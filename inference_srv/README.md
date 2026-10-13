# inference_srv

Batching inference server: groups inference-style requests into batches
before processing, instead of one at a time. Same idea as vLLM/Triton's
dynamic batching. Uses [`allocator`](../allocator) directly for the
batch input buffer; `BatchingQueue`'s worker thread follows the same
mutex/condition_variable pattern as [`threadpool`](../threadpool)
without calling into it. Networking is a
[loopnet](https://github.com/wang-owen/loopnet) `TcpServer`; see
[Serving](#serving) below.

## TODO

- `threadpool_dep` is also an unused link dependency
- Failures within a worker thread are not propagated upwards. If engine throws then `response.at` fails and worker terminates, and the affected clients never receive a reply.
- loopnet can't close a connection from the server side, so an oversized unterminated request is discarded with `ERR Request too large` rather than disconnecting the client.
- a shutdown causes all pending tasks to be dropped

## Why batching helps

A real inference call (and this stub's cost model) pays two kinds of
cost: a **fixed cost per call** (kernel launch, memory setup -- the same
whether the batch holds 1 request or 100) and a **variable cost per
item** (the actual compute, roughly linear in batch size). Processing
requests one at a time pays the fixed cost on every single request;
batching pays it once per batch and spreads it across every request in
that batch. The benchmark below shows the resulting speedup growing with
request volume, since more requests per batch means the fixed cost gets
divided further.

## Architecture: request flow

```
 loop thread                     BatchingQueue                    worker thread
      |                               |                                |
      |-- submit(req, callback) ----->| push {req,cb} onto pending_,   |
      |                               | notify_one()                  |
      |                               |                                |
      |                               |                       wait_until(deadline,
      |                               |                         pending_.size() >= max_batch_size_
      |                               |                         || stop_)
      |                               |                                |
      |                               |                     drain up to max_batch_size_
      |                               |                     entries, release the lock
      |                               |                                |
      |                               |                     handler_(batch, out_responses) --.
      |                               |                                |                       |
      |                               |                                |                       v
      |                               |                                |            InferenceEngine::run_batch()
      |                               |                                |              - carve input buffer from
      |                               |                                |                batch_arena_ (allocator::ArenaAllocator)
      |                               |                                |              - stub compute (fixed +
      |                               |                                |                per-item sleep)
      |                               |                                |              - batch_arena_.reset()
      |                               |                                |<-- out_responses filled -------------'
      |                               |                                |
      |                               |                     match each Response.id back to
      |                               |                     its PendingRequest by id (not
      |                               |                     position -- handler order isn't
      |                               |                     guaranteed)
      |                               |                                |
      |<---------- on_response(matching Response) ------------------- |
```

Two independent conditions can trigger a flush, whichever comes first:
- **Size-triggered**: `pending_.size()` reaches `max_batch_size_`.
- **Time-triggered**: `max_wait_` elapses since `wait_until`'s deadline
  was set, even with a partial batch -- this bounds worst-case per-request
  latency when traffic is too sparse to fill a batch.

## Serving

`main.cc` runs a single `loopnet::EventLoop` thread. Its message callback
parses each complete `INFER <id> <f1> ... <fn>\n` line and either replies
`ERR ...` immediately or `submit()`s the request to the `BatchingQueue`
without blocking. The response callback fires on the queue's worker
thread, but `TcpConnection` may only be touched from the loop thread, so
the callback formats the `OK ...` line and hands it back with
`EventLoop::queue_task()`. It holds the connection by `weak_ptr` in case
the client disconnects before its batch completes. A client may pipeline
several requests on one connection. Replies are not ordered relative to
each other: an `ERR` is sent right away, while an `OK` waits for its batch.

## Layout
    include/inference_srv/  public headers (#include "inference_srv/...")
    src/                     implementation
    main.cc                 tiny demo executable
    tests/                    Catch2 suite
    benchmarks/               Catch2 benchmarks (run via `meson test --benchmark`)

## Components
- `BatchingQueue` -- producer-consumer queue (mutex + condition_variable +
  a dedicated worker thread, in the spirit of `threadpool::ThreadPool`).
  `submit()` is the producer side, callable from any thread;
  `worker_loop()` is the single consumer that decides when to flush and
  drives the handler.
- `InferenceEngine` -- the stub "model." `run_batch()` processes a whole
  batch at once, using an `allocator::ArenaAllocator` sized for one
  batch's input buffer -- carve the batch's inputs out of one arena
  allocation instead of N heap allocations, then `reset()` the whole
  arena in O(1) once the batch is done.
- `BatchHandler` -- the `std::function` glue between the two: lets
  `BatchingQueue` stay ignorant of what "processing a batch" means, and
  lets tests inject a fake handler without touching `InferenceEngine`.

## Benchmark

`inference_bench.cc` compares naive one-at-a-time processing against
`BatchingQueue` (`max_batch_size=32`, `max_wait=10ms`) across request
counts, using `InferenceEngine`'s stub cost model (5ms fixed + 1ms/item):

    inference_srv throughput: naive (one-at-a-time) vs batched
        10 requests | naive:    73.69 ms | batched:    31.44 ms | speedup: 2.34x
        50 requests | naive:   372.36 ms | batched:    83.44 ms | speedup: 4.46x
       200 requests | naive:  1455.23 ms | batched:   272.06 ms | speedup: 5.35x
      1000 requests | naive:  7387.28 ms | batched:  1319.32 ms | speedup: 5.60x

Speedup grows with request volume rather than staying flat, which is the
signature of amortizing a fixed cost: as batches fill up, the fixed 5ms
gets divided across more requests, and the naive/batched ratio approaches
`max_batch_size` in the limit.
