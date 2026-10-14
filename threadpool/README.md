# threadpool

Work-stealing thread pool. Each worker runs its own queue of tasks and
steals from other workers when it runs out, keeping all threads busy
without a single shared queue.

```sh
meson test -C build threadpool
./build/threadpool/threadpool_bench '[!benchmark]'
```

## Benchmark

Including pool startup and teardown, 10 workers:

| Workload                          | Serial  | `ThreadPool`         |
|-----------------------------------|---------|----------------------|
| 100k trivial tasks (`fetch_add`)  | 0.20 ms | 79 ms                |
| 1k ~20 us tasks (xorshift loop)   | 18.8 ms | 2.5 ms (7.6x faster) |

Per-task scheduling overhead is ~800 ns, so the pool pays off only once
each task does real work.

## Limitations

- Queues are lock-based, not lock-free.
- `submit()` returns no future, and there's no way to wait for tasks
  other than destroying the pool.
