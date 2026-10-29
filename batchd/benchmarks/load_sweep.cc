#include "batchd/batchd.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <latch>
#include <semaphore>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "threadpool/thread_pool.h"

// Drives a BatchingQueue at a fixed request rate (open loop: request i is
// submitted at start + i / rate whether or not earlier ones have finished)
// and reports achieved throughput and latency percentiles. Batches run on a
// ThreadPool with at most one batch per worker in flight, as in main.cc.
// Latency is measured from each request's scheduled send time, so a
// generator that falls behind schedule doesn't hide queueing delay.

namespace {

using Clock = std::chrono::steady_clock;
using Millis = std::chrono::duration<double, std::milli>;

struct Options {
  std::size_t workers = 4;
  std::size_t max_batch_size = 32;
  std::chrono::milliseconds max_wait{5};
  double rate = 1000.0;
  std::size_t requests = 2000;
  bool csv = false;
};

void print_usage(const char *argv0) {
  std::cerr << std::format(
      "usage: {} [--workers N] [--max-batch-size N] [--max-wait-ms N]\n"
      "          [--rate REQ_PER_S] [--requests N] [--csv]\n",
      argv0);
}

template <typename T> bool parse_number(std::string_view text, T &out) {
  const char *last = text.data() + text.size();
  auto [end, ec] = std::from_chars(text.data(), last, out);
  return !text.empty() && ec == std::errc{} && end == last;
}

bool parse_args(int argc, char **argv, Options &opts) {
  for (int i = 1; i < argc; ++i) {
    std::string_view flag = argv[i];
    if (flag == "--csv") {
      opts.csv = true;
      continue;
    }
    if (i + 1 >= argc) {
      return false;
    }
    std::string_view value = argv[++i];
    bool ok = false;
    if (flag == "--workers") {
      ok = parse_number(value, opts.workers) && opts.workers > 0;
    } else if (flag == "--max-batch-size") {
      ok = parse_number(value, opts.max_batch_size) && opts.max_batch_size > 0;
    } else if (flag == "--max-wait-ms") {
      long long ms = 0;
      ok = parse_number(value, ms) && ms >= 0;
      opts.max_wait = std::chrono::milliseconds(ms);
    } else if (flag == "--rate") {
      ok = parse_number(value, opts.rate) && opts.rate > 0;
    } else if (flag == "--requests") {
      ok = parse_number(value, opts.requests) && opts.requests > 0;
    }
    if (!ok) {
      return false;
    }
  }
  return true;
}

// `sorted` must be non-empty and ascending.
Millis percentile(const std::vector<Millis> &sorted, double p) {
  std::size_t idx = static_cast<std::size_t>(p * (sorted.size() - 1) + 0.5);
  return sorted[idx];
}

} // namespace

int main(int argc, char **argv) {
  Options opts;
  if (!parse_args(argc, argv, opts)) {
    print_usage(argv[0]);
    return 2;
  }

  // Indexed by Request::id; each slot is written by exactly one pool task and
  // read only after `done` is released.
  std::vector<Clock::time_point> completed(opts.requests);
  std::latch done(static_cast<std::ptrdiff_t>(opts.requests));
  std::atomic<std::size_t> batches = 0;

  std::counting_semaphore<> in_flight_slots{
      static_cast<std::ptrdiff_t>(opts.workers)};
  threadpool::ThreadPool pool(opts.workers);
  {
    // Every request fits in pending_, so submit() never rejects and overload
    // shows up as latency rather than drops.
    batchd::BatchingQueue queue(
        opts.max_batch_size, opts.max_wait, /*max_pending=*/opts.requests,
        [&](std::vector<batchd::Request> batch) {
          in_flight_slots.acquire();
          pool.submit([&, batch = std::move(batch)] {
            thread_local batchd::InferenceEngine engine;
            std::vector<batchd::Response> responses;
            engine.run_batch(batch, responses);
            auto now = Clock::now();
            for (const batchd::Response &response : responses) {
              completed[response.id] = now;
            }
            batches.fetch_add(1, std::memory_order_relaxed);
            in_flight_slots.release();
            done.count_down(static_cast<std::ptrdiff_t>(responses.size()));
          });
        });

    const auto interval = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / opts.rate));
    const auto start = Clock::now();
    auto scheduled = [&](std::uint64_t id) {
      return start + interval * static_cast<Clock::rep>(id);
    };
    for (std::uint64_t id = 0; id < opts.requests; ++id) {
      std::this_thread::sleep_until(scheduled(id));
      if (!queue.submit({.id = id, .input = {1.0f, 2.0f, 3.0f}})) {
        std::cerr << "BatchingQueue rejected a request\n";
        return 1;
      }
    }
    done.wait();

    std::vector<Millis> latencies;
    latencies.reserve(opts.requests);
    for (std::size_t id = 0; id < opts.requests; ++id) {
      latencies.push_back(completed[id] - scheduled(id));
    }
    std::ranges::sort(latencies);
    const auto last = *std::ranges::max_element(completed);
    const double elapsed_s =
        std::chrono::duration<double>(last - start).count();
    const double throughput = opts.requests / elapsed_s;
    const double mean_batch =
        static_cast<double>(opts.requests) / batches.load();
    const Millis p50 = percentile(latencies, 0.50);
    const Millis p99 = percentile(latencies, 0.99);

    if (opts.csv) {
      std::cout << "workers,max_batch_size,max_wait_ms,offered_rate,requests,"
                   "throughput,p50_ms,p99_ms,mean_batch_size\n";
      std::cout << std::format(
          "{},{},{},{},{},{:.1f},{:.2f},{:.2f},{:.2f}\n", opts.workers,
          opts.max_batch_size, opts.max_wait.count(), opts.rate, opts.requests,
          throughput, p50.count(), p99.count(), mean_batch);
    } else {
      std::cout << std::format(
          "workers={} max_batch_size={} max_wait={}ms rate={}/s requests={}\n"
          "  throughput: {:.1f} req/s\n"
          "  latency:    p50 {:.2f} ms, p99 {:.2f} ms\n"
          "  mean batch: {:.2f}\n",
          opts.workers, opts.max_batch_size, opts.max_wait.count(), opts.rate,
          opts.requests, throughput, p50.count(), p99.count(), mean_batch);
    }
  }
}
