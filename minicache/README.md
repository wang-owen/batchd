# minicache

In-memory key-value server with LRU eviction and a line-based text
protocol, served over [loopnet](https://github.com/wang-owen/loopnet).

## How it works

- **`LruCache`** -- `std::list` of key/value pairs (front =
  most-recently-used) plus an `std::unordered_map` from key to list node,
  giving O(1) average `get`/`put`/`remove`. `get` and `put` move the key to
  the front; `put` on a full cache evicts the back entry first. Capacity 0
  throws `std::invalid_argument`. Not thread-safe.
- **`CommandDispatcher`** -- parses one line with `std::istringstream` and
  runs it against an `LruCache`:

  | Command         | Reply                                   |
  |-----------------|-----------------------------------------|
  | `SET key value` | `OK`                                    |
  | `GET key`       | the value, or `(nil)` on a miss         |
  | `DEL key`       | `OK`, or `FAIL` if the key wasn't there |

  Missing arguments give `ERR wrong number of arguments for '<cmd>'`, and
  anything else gives `ERR unknown command '<cmd>'`. Keys and values are
  single whitespace-delimited tokens; extra tokens are ignored.
- **`main.cc`** -- a cache of 1024 entries behind a `loopnet::TcpServer`
  on `127.0.0.1:9090`. The single event-loop thread splits complete
  `\n`-terminated lines out of each connection's buffer, dispatches them,
  and `send()`s the reply, so the cache needs no locking. `SIGINT` calls
  `EventLoop::stop()`, which sets an atomic flag and writes to the loop's
  wakeup pipe, and `main` returns.

## Build and run

Built by the top-level Meson project (see the [root README](../README.md)).

```sh
./build/minicache/minicache_main     # listens on 127.0.0.1:9090
meson test -C build minicache        # unit tests
./build/minicache/minicache_bench '[!benchmark]'  # benchmark
```

## Benchmark

`minicache_bench` cycles a `put` + `get` pair over twice as many keys as
the cache's 1024-entry capacity, so every `put` in steady state evicts.
One pair averages 104 ns (`debugoptimized`, AppleClang 21, Apple M1 Pro).
This measures `LruCache` alone, not the network path.

## Limitations

- Replies are sent with no terminator, so pipelined replies run together
  (`OKhello(nil)`) and a client can't tell where one ends.
- Values can't contain whitespace.
- There's no limit on line length; an unterminated line grows the
  connection's input buffer without bound.
- Port and capacity are compile-time constants in `main.cc`.
