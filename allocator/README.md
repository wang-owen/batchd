# allocator

Two fixed-capacity allocators, each backed by one `malloc`'d block taken
in the constructor (which throws `std::runtime_error` if `malloc` fails).
Neither is thread-safe.

- **`ArenaAllocator`** -- bump pointer. `allocate(size, alignment)` aligns
  and advances a cursor, returning `nullptr` once the block is full.
  There's no per-object free; `reset()` rewinds the cursor and invalidates
  every pointer handed out. Fits allocations that share one lifetime, such
  as one batch's scratch memory in `inference_srv`.
- **`SlabAllocator`** -- fixed-size chunks on an intrusive free list.
  `allocate()`/`deallocate()` pop/push the list head; `allocate()` returns
  `nullptr` when every chunk is in use. Chunk size is rounded up to a
  multiple of `alignof(std::max_align_t)` (and at least a pointer), so
  there's no external fragmentation but up to
  `alignof(std::max_align_t) - 1` bytes of padding per chunk.
  `deallocate()` checks with `assert` that the pointer belongs to the
  slab.

## Build and run

Built by the top-level Meson project (see the [root README](../README.md)).

```sh
meson test -C build allocator       # unit tests
./build/allocator/allocator_bench '[!benchmark]'  # benchmark
```

## Benchmark

`allocator_bench` times one 64-byte operation per iteration with Catch2
(`debugoptimized`, AppleClang 21, Apple M1 Pro):

| Operation                           | Mean    | vs malloc/free |
|-------------------------------------|---------|----------------|
| `ArenaAllocator::allocate()`        | 3.4 ns  | 3.4x faster    |
| `SlabAllocator` allocate/deallocate | 4.9 ns  | 2.4x faster    |
| `malloc`/`free`                     | 11.6 ns | baseline       |

The arena row includes an amortized `reset()` every 4096 allocations,
since Catch2 picks the iteration count and a fixed-size arena would
otherwise run dry. All three cases are single-threaded and allocate the
same size every time, which is the slab's best case and `malloc`'s
fast path.

## Limitations

- Capacity is fixed at construction; neither allocator grows.
- Not thread-safe; give each thread its own instance.
- The slab serves one size only; the arena can't free individual objects.
