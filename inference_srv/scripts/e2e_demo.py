#!/usr/bin/env python3
"""End-to-end demo against a running inference_srv_main.

Exercises the line protocol:

    request:  INFER <client_id> <f1> <f2> ... <fn>\\n
    response: OK <client_id> <o1> ... <on>\\n   or   ERR <client_id> <reason>\\n

Each check runs on its own connection and prints PASS/FAIL; the exit status
is the number of failed checks. Start the server first:

    build/inference_srv/inference_srv_main &
    python3 inference_srv/scripts/e2e_demo.py
"""

import argparse
import socket
import struct
import sys
import threading
import time

MAX_LINE_BYTES = 64 * 1024


class Client:
    def __init__(self, host, port, timeout):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buf = b""

    def send(self, data):
        self.sock.sendall(data.encode() if isinstance(data, str) else data)

    def read_line(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("server closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode()

    def read_lines(self, n):
        return [self.read_line() for _ in range(n)]

    def close(self):
        self.sock.close()


def expected_ok(client_id, inputs):
    # The stub engine doubles every input.
    return [client_id] + [x * 2.0 for x in inputs]


def parse_ok(line):
    parts = line.split()
    if len(parts) < 2 or parts[0] != "OK":
        return None
    return [int(parts[1])] + [float(p) for p in parts[2:]]


def infer_line(client_id, inputs):
    return "INFER {} {}\n".format(client_id, " ".join(str(x) for x in inputs))


class Demo:
    def __init__(self, args):
        self.args = args
        self.failures = 0

    def connect(self):
        return Client(self.args.host, self.args.port, self.args.timeout)

    def check(self, name, ok, detail=""):
        print("{} {}{}".format("PASS" if ok else "FAIL", name,
                               ": " + detail if detail else ""))
        if not ok:
            self.failures += 1

    def run(self, name, fn):
        try:
            fn()
        except Exception as e:  # report and keep going
            self.check(name, False, "{}: {}".format(type(e).__name__, e))

    def valid_request(self):
        c = self.connect()
        c.send(infer_line(1, [1.0, 2.5, -3.0]))
        reply = c.read_line()
        c.close()
        self.check("valid request", parse_ok(reply) == expected_ok(1, [1.0, 2.5, -3.0]),
                   repr(reply))

    def pipelined_requests(self):
        # All lines go out in one write; replies may come back in any order.
        n = 20
        c = self.connect()
        c.send("".join(infer_line(i, [float(i)]) for i in range(n)))
        replies = c.read_lines(n)
        c.close()
        by_id = {}
        for line in replies:
            parsed = parse_ok(line)
            if parsed is not None:
                by_id[parsed[0]] = parsed
        ok = all(by_id.get(i) == expected_ok(i, [float(i)]) for i in range(n))
        self.check("pipelined requests", ok,
                   "{} replies, {} distinct OK ids".format(len(replies), len(by_id)))

    def malformed_lines(self):
        cases = [
            ("HELLO\n", "ERR - invalid request"),
            ("INFER\n", "ERR - invalid request"),
            ("INFER 7\n", "ERR 7 invalid request"),
            ("INFER 8 not-a-float\n", "ERR 8 invalid request"),
            ("INFER abc 1.0\n", "ERR - invalid request"),
            ("infer 9 1.0\n", "ERR 9 invalid request"),
        ]
        c = self.connect()
        for line, want in cases:
            c.send(line)
            got = c.read_line()
            self.check("malformed {!r}".format(line.rstrip("\n")), got == want,
                       "got {!r}".format(got))
        # The connection is still usable afterwards.
        c.send(infer_line(10, [1.0]))
        got = c.read_line()
        c.close()
        self.check("valid request after malformed lines",
                   parse_ok(got) == expected_ok(10, [1.0]), repr(got))

    def oversized_lines(self):
        payload = " ".join(["1.0"] * (MAX_LINE_BYTES // 4 + 1000))

        # Terminated line over the limit. If more than 64 KiB of it is
        # buffered before its newline arrives, the server rejects it as
        # unterminated and later parses the tail as its own (invalid) line, so
        # one extra "ERR - invalid request" may precede the next reply.
        c = self.connect()
        c.send("INFER 42 " + payload + "\n")
        got = c.read_line()
        self.check("oversized terminated line", got == "ERR 42 request too large",
                   repr(got))
        c.send(infer_line(43, [2.0]))
        got = c.read_line()
        extra = []
        while got == "ERR - invalid request":
            extra.append(got)
            got = c.read_line()
        c.close()
        self.check("valid request after oversized line",
                   parse_ok(got) == expected_ok(43, [2.0]) and len(extra) <= 1,
                   repr(got) + (" after {} tail ERR".format(len(extra)) if extra else ""))

        # Unterminated line over the limit: the server discards what it has
        # buffered once it passes the limit, without waiting for a newline.
        c = self.connect()
        c.send("INFER 44 " + payload)
        got = c.read_line()
        self.check("oversized unterminated line", got == "ERR 44 request too large",
                   repr(got))
        c.close()

    def disconnect_before_reply(self):
        # Close right after sending, before any batch can finish (the server
        # waits up to 5 ms to fill a batch). SO_LINGER 0 makes the close an
        # RST, so the server's later send hits a reset connection.
        for linger in (False, True):
            for i in range(10):
                c = self.connect()
                c.send("".join(infer_line(100 + j, [1.0]) for j in range(5)))
                if linger:
                    c.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                      struct.pack("ii", 1, 0))
                c.close()
        time.sleep(0.2)
        c = self.connect()
        c.send(infer_line(200, [3.0]))
        got = c.read_line()
        c.close()
        self.check("server still serves after early disconnects",
                   parse_ok(got) == expected_ok(200, [3.0]), repr(got))

    def concurrent_clients(self):
        n_clients = self.args.clients
        per_client = self.args.requests_per_client
        results = [None] * n_clients

        def client(idx):
            try:
                c = self.connect()
                ids = [idx * per_client + j for j in range(per_client)]
                c.send("".join(infer_line(i, [float(i)]) for i in ids))
                ok = busy = bad = 0
                seen = set()
                for line in c.read_lines(per_client):
                    parsed = parse_ok(line)
                    if parsed is not None and parsed[0] in ids and \
                            parsed == expected_ok(parsed[0], [float(parsed[0])]):
                        ok += 1
                        seen.add(parsed[0])
                    elif line.endswith(" busy"):
                        busy += 1
                    else:
                        bad += 1
                c.close()
                results[idx] = (ok, busy, bad, len(seen) == ok)
            except Exception as e:
                results[idx] = e

        threads = [threading.Thread(target=client, args=(i,)) for i in range(n_clients)]
        start = time.monotonic()
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        elapsed = time.monotonic() - start

        errors = [r for r in results if isinstance(r, Exception)]
        done = [r for r in results if not isinstance(r, Exception)]
        ok = sum(r[0] for r in done)
        busy = sum(r[1] for r in done)
        bad = sum(r[2] for r in done)
        unique = all(r[3] for r in done)
        # "busy" is a valid reply once the server's pending queue is full.
        self.check("{} concurrent clients x {} requests".format(n_clients, per_client),
                   not errors and bad == 0 and unique,
                   "{} OK, {} busy, {} bad, {} client errors in {:.2f}s{}".format(
                       ok, busy, bad, len(errors), elapsed,
                       " (first: {})".format(errors[0]) if errors else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9091)
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="per-socket-operation timeout in seconds")
    parser.add_argument("--clients", type=int, default=64)
    parser.add_argument("--requests-per-client", type=int, default=5)
    args = parser.parse_args()

    demo = Demo(args)
    try:
        demo.connect().close()
    except OSError as e:
        print("cannot connect to {}:{}: {}. Is inference_srv_main running?".format(
            args.host, args.port, e), file=sys.stderr)
        return 1

    demo.run("valid request", demo.valid_request)
    demo.run("pipelined requests", demo.pipelined_requests)
    demo.run("malformed lines", demo.malformed_lines)
    demo.run("oversized lines", demo.oversized_lines)
    demo.run("disconnect before reply", demo.disconnect_before_reply)
    demo.run("concurrent clients", demo.concurrent_clients)

    print("{} check(s) failed".format(demo.failures) if demo.failures else "all checks passed")
    return min(demo.failures, 125)


if __name__ == "__main__":
    sys.exit(main())
