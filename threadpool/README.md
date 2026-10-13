# threadpool

Work-stealing thread pool that runs submitted tasks across N worker
threads.

## Design

Each `Worker` owns a `WorkStealingDeque` (a mutex-guarded `std::deque`).
`submit()` hands tasks to workers round-robin and releases one permit on a
shared `std::counting_semaphore`. A worker:

1. pops from the **back** of its own deque (LIFO, the most recently
   queued task);
2. if that's empty, tries to steal from the **front** (FIFO, the oldest
   task) of each other worker's deque in turn;
3. if nothing was found, exits when the pool is shutting down, otherwise
   blocks on the semaphore until more work arrives.

Stealing keeps every thread busy when one worker is stuck on a long task
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

`threadpool_bench` runs 100k trivial tasks (one relaxed `fetch_add` each),
including pool startup and teardown (`debugoptimized`/`-O2`, AppleClang 21,
Apple M1 Pro):

| Run                       | Mean     |
|---------------------------|----------|
| 1 thread (serial loop)    | 0.2 ms   |
| 10-worker `ThreadPool`    | 76 ms    |

The pool is far slower here because each task does almost no work: the
time goes to per-task overhead (`std::function` allocation, a deque lock,
a semaphore release) and contention on the shared counter. A pool pays off
only when tasks are large enough to amortize that overhead.
