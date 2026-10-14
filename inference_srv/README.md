# inference_srv

Batching inference server: it groups requests into batches before running
them, in the style of Triton's dynamic batching, so a fixed per-call cost
is paid once per batch instead of once per request. The "model" is a stub
that doubles every input and sleeps for 5 ms + 1 ms per request in the
batch (`InferenceEngine::batch_latency()`). Uses [`allocator`](../allocator)
for each batch's input buffer, [`threadpool`](../threadpool) to run
batches, and [loopnet](https://github.com/wang-owen/loopnet) for TCP.

## How it works

```
 loop thread               BatchingQueue worker            ThreadPool worker
      |-- submit(request) -------->| pending_.push_back()          |
      |                            | wait until size >= max_batch  |
      |                            |   or oldest + max_wait passed |
      |                            | move <= max_batch requests out|
      |                            |-- handler(batch): ----------->|
      |                            |   in_flight_slots.acquire(),  | InferenceEngine::run_batch()
      |                            |   pool.submit()               |   (thread_local engine/arena)
      |<-- queue_task(send reply) per request ---------------------|
      |                            |                               | in_flight_slots.release()
```

- **`BatchingQueue`** -- mutex + condition variable + one worker thread.
  The worker flushes when `max_batch_size` requests are pending or
  `max_wait` has passed since the oldest pending request was enqueued,
  and calls the `BatchHandler` outside the lock. `submit()` returns false
  once `max_pending` requests are waiting.
- **`InferenceEngine`** -- copies each request's input into a 4 MiB
  `ArenaAllocator` (reset at the start of every batch), produces one
  `Response` per `Request`, then sleeps for the simulated latency. Throws
  `std::runtime_error` if a batch's inputs don't fit in the arena.
- **`main.cc`** -- one `loopnet::EventLoop` thread, a `ThreadPool` of
  `max(2, hardware_concurrency())` workers, and a `BatchingQueue` with
  `max_batch_size=4`, `max_wait=50ms`, `max_pending=440`. The handler
  takes one of `num_workers` permits from a `std::counting_semaphore`
  before submitting the batch to the pool, and the task returns it, so at
  most one batch per worker is in flight. Once every worker is busy the
  backlog waits in `BatchingQueue` (FIFO) rather than the pool's deques
  (newest-first), and later flushes are full batches. `TcpConnection` may
  only be used on the loop thread, so replies go back through
  `EventLoop::queue_task()`; each `Request` holds its connection as a
  `shared_ptr`, and `send()` on a closed connection is a no-op.

## Protocol

Listens on `127.0.0.1:9091`. One request per `\n`-terminated line:

    request:  INFER <client_id> <f1> <f2> ... <fn>
    response: OK <client_id> <o1> ... <on>
          or: ERR <client_id> <reason>

`ERR` reasons: `invalid request` (not `INFER`, no inputs, or a token that
doesn't parse), `request too large` (a line, or unterminated input, over 64 KiB),
`busy` (`submit()` rejected it), and `engine failure` (`run_batch()`
threw; sent for every request in the batch). `<client_id>` in an `ERR`
echoes the line's second token if it parses as an unsigned integer, or
`-` otherwise. `ERR`s other than `engine failure` are sent immediately, while
`OK`s wait for their batch, so replies on one connection can arrive out of
order; match them by `client_id`.

## Build and run

Built by the top-level Meson project (see the [root README](../README.md)).

```sh
./build/inference_srv/inference_srv_main      # server on 127.0.0.1:9091
./build/inference_srv/inference_srv_client    # sends one INFER line, prints the reply
python3 inference_srv/scripts/e2e_demo.py     # protocol checks against the running server
meson test -C build inference_srv             # unit tests
./build/inference_srv/inference_srv_bench '[!benchmark]'  # batch-size sweep
inference_srv/scripts/load_sweep.sh out.csv   # open-loop load sweep (about 2 minutes)
```

`e2e_demo.py` runs valid, pipelined, malformed, and oversized requests,
clients that disconnect before their reply, and 64 concurrent clients
(`--clients`, `--requests-per-client`), printing PASS/FAIL per check and
exiting non-zero if any fail. `busy` replies count as valid.

## Benchmarks

Numbers from `debugoptimized`, AppleClang 21, Apple M1 Pro.

`inference_bench` sends 200 requests through a `BatchingQueue` (which
runs batches on its own worker thread with `max_wait=10ms`) at each max
batch size and compares against calling `run_batch()` once per request.
Times are the median of 5 runs. "Predicted" is the ratio of total
`batch_latency()` with and without batching:

| max_batch_size | Time       | req/s | Speedup | Predicted |
|---------------:|-----------:|------:|--------:|----------:|
|    naive       | 1535.32 ms |   130 |   1.00x |     1.00x |
|              1 | 1562.39 ms |   128 |   0.98x |     1.00x |
|              2 |  865.05 ms |   231 |   1.77x |     1.71x |
|              4 |  559.31 ms |   358 |   2.75x |     2.67x |
|              8 |  394.70 ms |   507 |   3.89x |     3.69x |
|             16 |  313.89 ms |   637 |   4.89x |     4.53x |
|             32 |  256.92 ms |   778 |   5.98x |     5.11x |
|             64 |  233.96 ms |   855 |   6.56x |     5.45x |

The naive run takes 1535 ms against the model's 1200 ms, so each
`run_batch()` call costs about 1.7 ms more than modeled (mostly
`sleep_for` oversleeping). Batching amortizes that per-call cost too,
which is why measured speedup exceeds the prediction at large batch sizes.

`load_sweep` drives a `BatchingQueue` and a `ThreadPool`, set up as in
`main.cc` but without networking, at a fixed offered rate (open loop) and
reports achieved throughput, p50/p99 latency from each request's scheduled
send time, and mean batch size:

    ./build/inference_srv/load_sweep --workers 4 --max-batch-size 32 \
        --max-wait-ms 5 --rate 2000 --requests 6000 [--csv]

`scripts/load_sweep.sh` runs it at 100-3000 req/s for three
configurations with 4 workers and 3 s of offered load per point
(`WORKERS`, `DURATION_S`, `RATES` and `LOAD_SWEEP` override). Selected
p50 / p99 latency in ms:

| Offered req/s | No batching (1, 0 ms) | Batch 32, 0 ms wait | Batch 32, 5 ms wait |
|--------------:|----------------------:|--------------------:|--------------------:|
|           200 |            8.5 / 13.0 |          8.5 / 12.2 |         13.2 / 19.4 |
|           600 |            180 / 348  |          9.6 / 18.9 |         14.9 / 32.2 |
|          1000 |          1247 / 2527  |         12.4 / 21.9 |         17.3 / 44.6 |
|          2000 |          3951 / 8038  |         24.1 / 64.2 |         22.6 / 49.8 |
|          3000 |          6742 / 13513 |         51.4 / 68.0 |         54.9 / 82.1 |

Without batching, throughput tops out at about 540 req/s, and above that
latency grows with the length of the run because the backlog is
unbounded. Both batching configurations kept up with every offered rate up
to 3000 req/s. With no wait, batches only form from backlog (mean size 1.0
at 200 req/s, 21.9 at 3000). The 5 ms wait adds about 5 ms at low load,
and at 2000 req/s its larger batches (11.8 vs 7.1 on average) gave lower
latency.

## Limitations

- The stub model sleeps instead of computing, so concurrent batches don't
  compete for CPU and each worker acts as an independent model replica
  (10 on an M1 Pro in `main.cc`, 4 in the load sweep). Copies sharing one
  GPU would slow each other down, so these numbers overstate what N
  replicas on one device would reach.
- Shutdown drops every request still pending in `BatchingQueue` without
  handing it to the handler, and `SIGINT` calls `_exit(0)`, so in-flight
  clients never get a reply.
- loopnet can't close a connection from the server side, so an oversized
  line is discarded rather than disconnecting the client. Once more than
  64 KiB is buffered without a newline, the buffer is dropped, and the rest
  of that line is later parsed as a new line, giving an extra
  `ERR - invalid request`.
- Parsing uses `std::stoull`/`std::stof`, which accept a valid prefix:
  `1.5x` is read as 1.5, `12abc` as client id 12, `-1` wraps to
  18446744073709551615, and `nan`/`inf` are accepted as inputs.
- Port, batch size, max wait and queue cap are compile-time constants in
  `main.cc`.
