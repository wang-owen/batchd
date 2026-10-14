# minicache

In-memory key-value server with LRU eviction and a line-based text
protocol (`SET`, `GET`, `DEL`), served over
[loopnet](https://github.com/wang-owen/loopnet) on `127.0.0.1:9090`.

```
SET key value   ->  OK
GET key         ->  value, or (nil)
DEL key         ->  OK, or FAIL
```

Everything runs on a single event-loop thread, so the cache needs no
locking.

```sh
./build/minicache/minicache_main
meson test -C build minicache
./build/minicache/minicache_bench '[!benchmark]'
```

## Benchmark

| Operation                                | Mean   |
|------------------------------------------|--------|
| `put` + `get` (every `put` evicts)       | 104 ns |

This measures the cache alone, not the network path.

## Limitations

- Keys and values can't contain whitespace.
- Port and capacity are hard-coded in `main.cc`.
