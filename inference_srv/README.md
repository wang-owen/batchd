# inference_srv

Batching inference server: groups inference-style requests into batches
before processing, instead of one at a time. Same idea as Triton's
dynamic batching. Uses [`allocator`](../allocator) for each batch's input
buffer and [`threadpool`](../threadpool) to run batches off the queue's
worker thread. Networking is a
[loopnet](https://github.com/wang-owen/loopnet) `TcpServer`; see
[Serving](#serving) below.

## TODO

- A shutdown drops every request still pending in `BatchingQueue` without
  handing it to the handler, and `SIGINT` exits immediately, so in-flight
  clients never receive a reply.
- loopnet can't close a connection from the server side, so an oversized
  unterminated request is discarded with `ERR request too large` rather
  than disconnecting the client.

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
 loop thread               BatchingQueue worker            ThreadPool worker
      |                            |                               |
      |-- submit(request) -------->| pending_.push_back(),         |
      |                            | notify_one()                  |
      |                            |                               |
      |                            | wait_until(oldest enqueue_time|
      |                            |   + max_wait_, size >=        |
      |                            |   max_batch_size_ || stop_)   |
      |                            |                               |
      |                            | move up to max_batch_size_    |
      |                            | requests out, release lock    |
      |                            |                               |
      |                            |-- handler_(batch) ----------->|
      |                            |   (pool.submit)               |
      |                            |                               | InferenceEngine::run_batch()
      |                            |                               |   - batch_arena_.reset()
      |                            |                               |   - copy inputs into arena
      |                            |                               |   - stub compute (fixed +
      |                            |                               |     per-item sleep)
      |                            |                               |
      |<-- queue_task(send "OK ...") per response -----------------|
      |    (or "ERR engine failure" per request if run_batch threw)|
```

Two independent conditions can trigger a flush, whichever comes first:
- **Size-triggered**: `pending_.size()` reaches `max_batch_size_`.
- **Time-triggered**: `max_wait_` elapses since the oldest pending request
  was enqueued, even with a partial batch -- this bounds worst-case
  per-request latency when traffic is too sparse to fill a batch.

## Serving

`main.cc` runs a single `loopnet::EventLoop` thread. Its message callback
splits complete `\n`-terminated lines out of the connection's buffer:

    request:  INFER <client_id> <f1> <f2> ... <fn>\n
    response: OK <client_id> <o1> ... <on>\n   or   ERR <reason>\n

Malformed lines get `ERR invalid request` and lines over 64 KiB get
`ERR request too large`, both sent immediately. Valid lines become a
`Request` tagged with a server-assigned id (`BatchingQueue::get_id()`) and
are `submit()`ted without blocking.

The batch handler runs on the queue's worker thread and only hands the
batch to a `threadpool::ThreadPool`, so the queue keeps accumulating the
next batch while earlier ones are computed. Each pool thread owns a
`thread_local InferenceEngine` (and so its own arena). `TcpConnection` may
only be touched from the loop thread, so results go back through
`EventLoop::queue_task()`. Each `Request`/`Response` holds its connection
as a `shared_ptr`; if the client has disconnected by then, `send()` is a
no-op. A client may pipeline several requests on one connection. Replies
are not ordered relative to each other: an `ERR` is sent right away, while
an `OK` waits for its batch.

`client.cc` is a minimal client that sends one `INFER` line to
`127.0.0.1:9091` and prints the reply.

## Layout
    include/inference_srv/  public headers (#include "inference_srv/...")
    src/                    implementation
    main.cc                 server executable
    client.cc               one-shot test client
    tests/                  Catch2 suite
    benchmarks/             Catch2 benchmarks (run via `meson test --benchmark`)

## Components
- `Request` / `Response` -- plain structs carrying the server-assigned
  `id`, the client's `client_id`, the originating connection, and the
  input/output vectors.
- `BatchingQueue` -- producer-consumer queue (mutex + condition_variable +
  a dedicated worker thread). `submit()` is the producer side, callable
  from any thread; `worker_loop()` is the single consumer that decides
  when to flush and calls the handler outside the lock.
- `BatchHandler` -- `std::function<void(std::vector<Request>)>`, the glue
  between the queue and whatever processes a batch. Keeps `BatchingQueue`
  ignorant of what "processing a batch" means and lets tests inject a
  fake handler without touching `InferenceEngine`.
- `InferenceEngine` -- the stub "model." `run_batch()` processes a whole
  batch at once, carving every request's input out of one 4 MiB
  `allocator::ArenaAllocator` instead of N heap allocations, and
  `reset()`s the arena in O(1) at the start of the next batch. Throws
  `std::runtime_error` if a batch's inputs don't fit in the arena.

## Benchmark

`inference_bench.cc` compares naive one-at-a-time processing against
`BatchingQueue` (`max_batch_size=32`, `max_wait=10ms`) across request
counts, using `InferenceEngine`'s stub cost model (5ms fixed + 1ms/item):

| Requests | Naive      | Batched    | Speedup |
|---------:|-----------:|-----------:|--------:|
|       10 |   73.36 ms |   30.59 ms |   2.40x |
|       50 |  367.58 ms |   69.14 ms |   5.32x |
|      200 | 1466.60 ms |  258.92 ms |   5.66x |
|     1000 | 7371.79 ms | 1276.89 ms |   5.77x |

(`debugoptimized`/`-O2`, AppleClang 21, Apple M1 Pro.)

Speedup grows with request volume until batches are full, which is the
signature of amortizing a fixed cost: as batches fill up, the fixed 5ms
gets divided across more requests. The per-item 1ms can't be amortized,
so the ratio levels off at (5 + 1) / (5/32 + 1) ≈ 5.2x with full batches
of 32, plus whatever per-call overhead (e.g. `sleep_for` oversleeping)
batching also saves.
