# inference-server

C++23 building blocks for a batching inference server, plus two servers
built from them. Each folder is its own Meson static library; components
link against each other rather than copying code. Networking comes from
[loopnet](https://github.com/wang-owen/loopnet), a single-threaded
`poll()`-based TCP library pulled in as a Meson subproject. The
"inference" is a stub that doubles its inputs and sleeps for a simulated
model latency.

| Folder | Description | Depends on |
|---|---|---|
| [`allocator/`](allocator) | Arena and slab allocators | -- |
| [`threadpool/`](threadpool) | Work-stealing thread pool | threads |
| [`minicache/`](minicache) | LRU key-value server | loopnet (server executable only) |
| [`inference_srv/`](inference_srv) | Batching inference server | allocator, threadpool, loopnet |

## Layout

Every component follows the same shape:

    <component>/
      include/<component>/   public headers, #include "<component>/..."
      src/                   implementation
      main.cc                server executable (minicache, inference_srv only)
      tests/                 Catch2 suite
      benchmarks/            Catch2 benchmarks (run via `meson test --benchmark`)
      meson.build
      README.md

## Build

Requires Meson >= 1.3, Ninja, and a C++23 compiler (`std::format`,
`std::ranges::to`); developed with AppleClang 21. Catch2 3.16.0 and loopnet
are fetched through `subprojects/catch2.wrap` and `subprojects/loopnet.wrap`
(loopnet is pinned to a commit; bump `revision` there to update).

```sh
meson setup build                 # .clangd expects the build dir to be named build
meson compile -C build
meson test -C build               # all unit tests
meson test -C build --benchmark   # all benchmarks (add -v to see their output)
./build/allocator/allocator_bench '[!benchmark]'  # one benchmark binary
```

Benchmark test cases are tagged `[!benchmark]`, which Catch2 hides, so a
`build/<component>/<component>_bench` binary run without that tag runs
nothing.

The default build type is `debugoptimized` (`-O2`, asserts on). Options,
both features defaulting to `enabled`: `-Dtests=disabled` and
`-Dbenchmarks=disabled`. Sanitizers use Meson's built-in option:
`-Db_sanitize=thread` or `-Db_sanitize=address`.

Benchmark numbers in the component READMEs come from a `debugoptimized`
build with AppleClang 21 on an Apple M1 Pro (8 performance + 2 efficiency
cores), macOS.
