#include "inference_srv/inference_srv.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using Millis = std::chrono::duration<double, std::milli>;

std::vector<inference_srv::Request> make_requests(std::size_t count) {
  std::vector<inference_srv::Request> requests;
  requests.reserve(count);
  for (std::uint64_t i = 0; i < count; ++i) {
    requests.push_back({.id = i, .input = {1.0f, 2.0f, 3.0f}});
  }
  return requests;
}

// Baseline: process one request at a time, each as its own "batch" of 1
std::size_t run_naive(inference_srv::InferenceEngine &engine,
                      const std::vector<inference_srv::Request> &requests) {
  std::vector<inference_srv::Response> responses;
  std::size_t total = 0;
  for (const auto &req : requests) {
    std::vector<inference_srv::Request> single{req};
    engine.run_batch(single, responses);
    total += responses.size();
  }
  return total;
}

// Batched: submits requests through one long-lived BatchingQueue and waits
// for every response, letting the queue group them by max_batch_size/max_wait.
// The queue outlives each timed run so its thread startup and shutdown aren't
// measured. `max_pending` must cover a whole run, since every request is
// submitted before any response is awaited.
class BatchedRunner {
public:
  BatchedRunner(std::size_t max_batch_size, std::chrono::milliseconds max_wait,
                std::size_t max_pending)
      : queue_(max_batch_size, max_wait, max_pending,
               [this](const std::vector<inference_srv::Request> &batch) {
                 engine_.run_batch(batch, responses_);
                 std::lock_guard<std::mutex> lock(mtx_);
                 received_ += responses_.size();
                 cv_.notify_all();
               }) {}

  std::size_t run(const std::vector<inference_srv::Request> &requests) {
    {
      std::lock_guard<std::mutex> lock(mtx_);
      received_ = 0;
    }
    for (const auto &req : requests) {
      if (!queue_.submit(req)) {
        throw std::runtime_error("BatchingQueue rejected a request");
      }
    }
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [&] { return received_ >= requests.size(); });
    return received_;
  }

private:
  inference_srv::InferenceEngine engine_;
  // Only touched by the queue's worker thread.
  std::vector<inference_srv::Response> responses_;
  std::mutex mtx_;
  std::condition_variable cv_;
  std::size_t received_ = 0;
  inference_srv::BatchingQueue queue_;
};

// Median wall time of `repetitions` calls to `fn`.
template <typename Fn> Millis median_time(int repetitions, Fn &&fn) {
  std::vector<Millis> samples;
  for (int i = 0; i < repetitions; ++i) {
    auto start = std::chrono::steady_clock::now();
    fn();
    samples.push_back(std::chrono::steady_clock::now() - start);
  }
  std::ranges::sort(samples);
  return samples[samples.size() / 2];
}

// Total simulated latency of `count` requests split into full batches of
// `max_batch_size` plus one partial batch, per InferenceEngine's cost model.
Millis predicted_time(std::size_t count, std::size_t max_batch_size) {
  using inference_srv::InferenceEngine;
  Millis total = InferenceEngine::batch_latency(max_batch_size) *
                 static_cast<double>(count / max_batch_size);
  if (count % max_batch_size != 0) {
    total += InferenceEngine::batch_latency(count % max_batch_size);
  }
  return total;
}

} // namespace

// Prints a table instead of using BENCHMARK so the measured speedup can be
// shown next to the cost model's prediction.
TEST_CASE("inference_srv throughput across max batch sizes",
          "[!benchmark][inference_srv]") {
  constexpr std::size_t kRequests = 200;
  constexpr int kRepetitions = 5;
  constexpr std::size_t kBatchSizes[] = {1, 2, 4, 8, 16, 32, 64};
  const auto requests = make_requests(kRequests);

  inference_srv::InferenceEngine engine;
  const Millis naive =
      median_time(kRepetitions, [&] { run_naive(engine, requests); });
  const Millis predicted_naive = predicted_time(kRequests, 1);

  std::cout << std::format(
      "{} requests, median of {} runs\n"
      "{:>14} {:>11} {:>12} {:>9} {:>10}\n",
      kRequests, kRepetitions, "max_batch_size", "time (ms)", "req/s",
      "speedup", "predicted");
  std::cout << std::format("{:>14} {:>11.2f} {:>12.0f} {:>8.2f}x {:>9.2f}x\n",
                           "naive", naive.count(),
                           kRequests / (naive.count() / 1000.0), 1.0, 1.0);

  for (std::size_t max_batch_size : kBatchSizes) {
    BatchedRunner batched(max_batch_size, std::chrono::milliseconds(10),
                          /*max_pending=*/kRequests);
    const Millis measured =
        median_time(kRepetitions, [&] { batched.run(requests); });
    std::cout << std::format(
        "{:>14} {:>11.2f} {:>12.0f} {:>8.2f}x {:>9.2f}x\n", max_batch_size,
        measured.count(), kRequests / (measured.count() / 1000.0),
        naive / measured,
        predicted_naive / predicted_time(kRequests, max_batch_size));
  }
}
