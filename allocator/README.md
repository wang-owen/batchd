# allocator

Arena/slab allocator library, benchmarked against `malloc`.

## Benchmark

`allocator_bench` times one 64-byte operation per iteration with Catch2
(`debugoptimized`/`-O2`, AppleClang 21, Apple M1 Pro):

| Operation                           | Mean    | vs malloc    |
|-------------------------------------|---------|--------------|
| `ArenaAllocator::allocate()`        | 3.6 ns  | ~3.2x faster |
| `SlabAllocator` allocate/deallocate | 4.7 ns  | ~2.5x faster |
| `malloc`/`free`                     | 11.6 ns | baseline     |

The arena row includes an amortized `reset()` every 4096 allocations,
since a fixed-size arena would otherwise run dry. Numbers are
platform-dependent — re-run `allocator_bench` locally rather than
trusting these exactly.

## Tradeoffs

- **Arena**: fastest — `allocate()` is a pointer bump, no bookkeeping. No
  per-object `free()`, only `reset()`, which invalidates everything at
  once. Fits allocations that share one lifetime (e.g. one request's
  scratch memory).
- **Slab**: nearly as fast as the arena — `allocate()`/`deallocate()` are
  a free-list pop/push, much simpler than general heap logic — and unlike
  the arena it frees chunks individually. Its other win over `malloc` is
  no fragmentation — every chunk is the same size, so freed
  chunks always fit the next request. Fits repeated same-size alloc/free
  (e.g. fixed-size cache entries, KV-cache pages).
- **`malloc`**: most flexible — arbitrary sizes, individual lifetimes, no
  upfront capacity — which is exactly what costs it speed here.

