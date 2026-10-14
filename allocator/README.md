# allocator

Two fixed-capacity, single-threaded allocators:

- **`ArenaAllocator`** -- bump allocator for objects that share one
  lifetime; everything is freed at once with `reset()`. Used for per-batch
  scratch memory in `inference_srv`.
- **`SlabAllocator`** -- pool of same-size chunks with O(1) allocate and
  free.

```sh
meson test -C build allocator
./build/allocator/allocator_bench '[!benchmark]'
```

## Benchmark

One 64-byte allocation per iteration:

| Operation                           | Mean    | vs malloc/free |
|-------------------------------------|---------|----------------|
| `ArenaAllocator::allocate()`        | 3.4 ns  | 3.4x faster    |
| `SlabAllocator` allocate/deallocate | 4.9 ns  | 2.4x faster    |
| `malloc`/`free`                     | 11.6 ns | baseline       |

Both beat `malloc` by skipping general-purpose bookkeeping.
