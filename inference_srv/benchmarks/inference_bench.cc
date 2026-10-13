#include "inference_srv/inference_srv.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {

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

} // namespace

TEST_CASE("inference_srv throughput: naive (one-at-a-time) vs batched",
          "[!benchmark][inference_srv]") {
  const std::size_t count = GENERATE(10, 50, 200, 1000);
  const auto requests = make_requests(count);
  const std::string suffix = " | " + std::to_string(count) + " requests";

  inference_srv::InferenceEngine engine;
  BENCHMARK("naive" + suffix) { return run_naive(engine, requests); };

  BatchedRunner batched(/*max_batch_size=*/32, std::chrono::milliseconds(10),
                        /*max_pending=*/count);
  BENCHMARK("batched" + suffix) { return batched.run(requests); };
}
