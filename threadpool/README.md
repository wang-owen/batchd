# threadpool

Work-stealing thread pool that runs `std::function<void()>` tasks on N
worker threads (`ThreadPool(n)` throws `std::invalid_argument` for 0).

## How it works

Each `Worker` owns a `WorkStealingDeque`, a mutex-guarded `std::deque`.
`submit()` from outside the pool picks a worker round-robin; `submit()`
from one of the pool's own workers pushes onto that worker's deque. Either
way it then releases one permit on a shared `std::counting_semaphore`.
A worker loops:

1. pop from the **back** of its own deque (newest task first);
2. otherwise try to steal from the **front** (oldest task) of each other
   worker's deque in turn;
3. otherwise exit if the pool is shutting down, or block on the semaphore.

Keeping subtasks on the spawning worker means they run next on the same
thread while shared data is still in cache; stealing keeps idle threads
busy when one worker's deque backs up behind a long task.

The destructor sets `shutdown_`, releases one permit per thread, and joins
every worker. Tasks already queued still run, since workers only check for
shutdown when they find no work; `submit()` calls after shutdown has
started are ignored. A task must not throw: an escaping exception calls
`std::terminate`. There is no way to wait for submitted tasks other than
destroying the pool, and `submit()` returns no future.

## Build and run

Built by the top-level Meson project (see the [root README](../README.md)).

```sh
meson test -C build threadpool       # unit tests
./build/threadpool/threadpool_bench '[!benchmark]'  # benchmark
```

## Benchmark

`threadpool_bench` times two workloads, each including pool startup and
teardown, with one worker per hardware thread (`debugoptimized`,
AppleClang 21, Apple M1 Pro, 10 workers):

| Workload                          | 1 thread (serial) | 10-worker `ThreadPool` |
|-----------------------------------|-------------------|------------------------|
| 100k trivial tasks (`fetch_add`)  | 0.20 ms           | 79 ms                  |
| 1k ~20 us tasks (xorshift loop)   | 18.8 ms           | 2.5 ms (7.6x faster)   |

With trivial tasks the pool is about 390x slower: ~2 ns per task serially
versus ~790 ns pooled. Each pooled task pays for two deque mutex
acquisitions (push and pop, plus more while idle workers scan for work to
steal), a semaphore release/acquire shared by every thread, and the
counter's cache line moving between cores. With ~20 us of work per task
that overhead is amortized. The pooled ~20 us case is noisy: one of two
runs averaged 3.3 ms with a 2.5 ms standard deviation.

## Limitations

- Deques are mutex-guarded `std::deque`s, not lock-free Chase-Lev deques,
  so every push, pop and steal takes a lock.
- No futures, task results, or wait-for-idle; only destruction waits.
- Permits are released per `submit()` regardless of which worker runs the
  task, so a woken worker may find nothing to do and go back to waiting.
