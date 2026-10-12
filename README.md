# loopnet

A small single-threaded reactor-style TCP networking library in C++23, built on `poll(2)`.

## Components

- `EventLoop` — runs the poll loop and supports cross-thread task queuing with a pipe-based wakeup
- `Channel` / `Poller` — fd event registration and dispatch
- `Acceptor` — non-blocking listening socket
- `TcpConnection` / `TcpServer` — connection lifecycle with message and close callbacks
- `Buffer` — byte buffer for reads and writes

## Build

Requires Meson ≥ 1.3 and a C++23 compiler.

```sh
meson setup build
meson compile -C build
```

Pass `-Dtests=false` to skip fetching Catch2.

## Run

`loopnet_server` listens on `127.0.0.1:9090` and prints each message it receives:

```sh
./build/loopnet_server
echo hello | nc 127.0.0.1 9090
```
