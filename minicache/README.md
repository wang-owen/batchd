# minicache

Networked key-value store (mini Redis) with a text protocol and LRU
eviction, built on top of [loopnet](https://github.com/wang-owen/loopnet).

## Layout
    include/minicache/  public headers (#include "minicache/...")
    src/                implementation
    main.cc             server executable (listens on 127.0.0.1:9090)
    tests/              Catch2 suite
    benchmarks/         Catch2 benchmarks (run via `meson test --benchmark`)

## Design
`LruCache` implements O(1) get/put/remove with LRU eviction
(`std::list` + `std::unordered_map`, front = most-recently-used).
`CommandDispatcher` parses `SET`/`GET`/`DEL` lines and returns `OK`,
the value, `(nil)` on a GET miss, or an `ERR ...` string for malformed
or unrecognized commands. `main.cc` wires the dispatcher into a
`loopnet::TcpServer`: every connection is non-blocking and serviced by the
single event-loop thread, whose message callback splits complete
`\n`-terminated lines out of the connection's input buffer and replies with
`TcpConnection::send()`. Because all dispatching happens on that one
thread, `LruCache`/`CommandDispatcher` need no locking despite not being
internally synchronized. `SIGINT` triggers a graceful shutdown via
`EventLoop::stop()`, which only sets a flag and writes to the loop's
self-pipe and so is safe to call from a signal handler.

## Benchmark

`minicache_bench` cycles a `put` + `get` pair over twice as many keys as
the cache's 1024-entry capacity, so every `put` in steady state evicts.
One pair averages ~107 ns (`debugoptimized`/`-O2`, AppleClang 21, Apple
M1 Pro).
