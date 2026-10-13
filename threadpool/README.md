# threadpool

Work-stealing thread pool that runs submitted tasks across N worker
threads.

## Design

Each `Worker` owns a `WorkStealingDeque` (a mutex-guarded `std::deque`).
`submit()` hands tasks from outside the pool to workers round-robin, while a
task submitted from inside a worker goes on that worker's own deque, and
releases one permit on a shared `std::counting_semaphore`. A worker:

1. pops from the **back** of its own deque (LIFO, the most recently
   queued task);
2. if that's empty, tries to steal from the **front** (FIFO, the oldest
   task) of each other worker's deque in turn;
3. if nothing was found, exits when the pool is shutting down, otherwise
   blocks on the semaphore until more work arrives.

Keeping subtasks local means a task that spawns children has them run next
on the same thread, while the data they share is still in cache. Stealing
keeps every thread busy when one worker is stuck on a long task
while its own deque fills up. On destruction the pool sets `shutdown_`,
releases one permit per thread, and joins every worker; tasks already
queued still run, while `submit()` calls made after shutdown starts are
dropped. Tasks must handle their own exceptions -- one escaping a task
calls `std::terminate`.

## Layout
    include/threadpool/  public headers (#include "threadpool/...")
    src/                 implementation
    tests/               Catch2 suite
    benchmarks/          Catch2 benchmarks (run via `meson test --benchmark`)

## Benchmark

`threadpool_bench` times two workloads, each including pool startup and
teardown (`debugoptimized`/`-O2`, AppleClang 21, Apple M1 Pro):

| Workload                          | 1 thread (serial) | 10-worker `ThreadPool` |
|-----------------------------------|-------------------|------------------------|
| 100k trivial tasks (`fetch_add`)  | 0.21 ms           | 78 ms                  |
| 1k ~20 us tasks (xorshift loop)   | 18.7 ms           | 2.6 ms (7.2x faster)   |

With trivial tasks the pool is about 400x slower: each task costs ~2 ns
serially but ~780 ns pooled, almost all of it scheduling overhead -- two
contended deque mutexes per task (more while idle workers scan for work to
steal), a semaphore release/acquire on an atomic shared by every thread,
and the counter's cache line bouncing between cores. Once each task does
tens of microseconds of work, that overhead is amortized and the pool
scales close to the core count (the M1 Pro has 8 performance and 2
efficiency cores).
