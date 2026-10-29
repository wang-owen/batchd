# batchd

Batching inference server. Incoming requests are grouped into batches,
like Triton's dynamic batching, so the fixed per-call cost of the model is
paid once per batch instead of once per request. The "model" is a stub
that doubles each input and sleeps to simulate latency.

Built from [`allocator`](../allocator) (per-batch buffers),
[`threadpool`](../threadpool) (runs batches), and
[loopnet](https://github.com/wang-owen/loopnet) (TCP).

## How it works

1. The network thread parses each request and adds it to a queue.
2. The queue flushes a batch once it's full or the oldest request has
   waited long enough.
3. A thread-pool worker runs the batch and hands replies back to the
   network thread.

When every worker is busy, requests wait in the queue and later batches
come out full. If the queue itself fills up, new requests are rejected
with `busy`.

## Protocol

Listens on `127.0.0.1:9091`, one request per line:

    request:  INFER <client_id> <f1> <f2> ... <fn>
    response: OK <client_id> <o1> ... <on>
          or: ERR <client_id> <reason>

Replies can arrive out of order; match them by `client_id`.

## Build and run

```sh
./build/batchd/batchd                       # server on 127.0.0.1:9091
./build/batchd/batchd_client                # sends one request
python3 batchd/scripts/e2e_demo.py          # end-to-end checks against the server
meson test -C build batchd
./build/batchd/batchd_bench '[!benchmark]'  # batch-size sweep
batchd/scripts/load_sweep.sh out.csv        # load sweep (~10 min)
```

## Benchmarks

**Throughput** -- 200 requests at each max batch size, versus running
each request alone (median of 5 runs):

| max_batch_size | Time       | req/s | Speedup |
|---------------:|-----------:|------:|--------:|
|    naive       | 1535.32 ms |   130 |   1.00x |
|              1 | 1562.39 ms |   128 |   0.98x |
|              2 |  865.05 ms |   231 |   1.77x |
|              4 |  559.31 ms |   358 |   2.75x |
|              8 |  394.70 ms |   507 |   3.89x |
|             16 |  313.89 ms |   637 |   4.89x |
|             32 |  256.92 ms |   778 |   5.98x |
|             64 |  233.96 ms |   855 |   6.56x |

Batching gives up to a 6.6x speedup.

**Latency under load** -- p50 / p99 latency in ms at a fixed offered rate,
4 workers (median of 5 runs):

| Offered req/s | No batching | Batch 32, no wait | Batch 32, 5 ms wait |
|--------------:|------------:|------------------:|--------------------:|
|           200 |  8.5 / 10.3 |        8.4 / 16.9 |         13.5 / 22.1 |
|           600 |   150 / 303 |        9.2 / 17.1 |         13.7 / 25.6 |
|          1000 | 1230 / 2499 |       12.7 / 23.8 |         16.2 / 30.5 |
|          2000 | 3982 / 7964 |       23.4 / 44.0 |         21.3 / 40.3 |
|          3000 | 6835 / 13712 |      51.8 / 70.5 |         48.4 / 67.2 |

Without batching the server saturates at ~540 req/s and latency grows
without bound. With batching it keeps up through 3000 req/s. A short wait
costs ~5 ms at low load but lowers latency at high load.

## Limitations

- The stub model sleeps instead of computing, so workers behave like
  independent model replicas. Real replicas sharing a GPU would contend,
  so the numbers are optimistic.
- Shutdown drops pending requests without replying.
- Oversized lines are discarded rather than closing the connection.
- Number parsing is lenient (accepts trailing garbage, `nan`, `inf`).
- Port, batch size, and wait time are hard-coded in `main.cc`.
