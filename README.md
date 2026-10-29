# batchd

C++23 building blocks for a batching inference server, plus the server
built from them. The "inference" is a stub that simulates model latency.

| Folder | Description |
|---|---|
| [`allocator/`](allocator) | Arena and slab allocators |
| [`threadpool/`](threadpool) | Work-stealing thread pool |
| [`loopnet/`](loopnet) | Single-threaded reactor TCP library |
| [`batchd/`](batchd) | Batching inference server |

## Build

Requires Meson >= 1.3, Ninja, and a C++23 compiler. Catch2 is
fetched automatically.

```sh
meson setup build
meson compile -C build
meson test -C build               # unit tests
meson test -C build --benchmark   # benchmarks
```

Benchmark results were measured on an Apple M1 Pro.
