#include "batchd/batching_queue.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using batchd::BatchingQueue;
using batchd::Request;

namespace {

// Comfortably above what any test submits, except the cap test itself.
constexpr std::size_t kMaxPending = 64;

// Thread-safe recorder for batches handed to the handler on the queue's
// worker thread. Tests block on wait_for_at_least() instead of sleeping a
// fixed amount, since submit() is fire-and-forget.
class BatchCollector {
public:
  void add(const std::vector<Request> &batch) {
    std::lock_guard<std::mutex> lock(mtx_);
    batches_.push_back(batch);
    request_count_ += batch.size();
    cv_.notify_all();
  }

  // Returns false (times out) if `count` requests never arrive within
  // `timeout` -- used to assert something did NOT happen promptly.
  bool wait_for_at_least(std::size_t count, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mtx_);
    return cv_.wait_for(lock, timeout, [&] { return request_count_ >= count; });
  }

  std::vector<std::vector<Request>> snapshot() {
    std::lock_guard<std::mutex> lock(mtx_);
    return batches_;
  }

  batchd::BatchHandler handler() {
    return [this](const std::vector<Request> &batch) { add(batch); };
  }

private:
  std::mutex mtx_;
  std::condition_variable cv_;
  std::vector<std::vector<Request>> batches_;
  std::size_t request_count_ = 0;
};

} // namespace

TEST_CASE("ConstructsAndDestructsCleanly", "[batching_queue]") {
  BatchingQueue queue(8, std::chrono::milliseconds(5), kMaxPending,
                      [](const std::vector<Request> &) {});
  SUCCEED();
}

TEST_CASE("FullBatchTriggersBeforeMaxWaitElapses", "[batching_queue]") {
  constexpr std::size_t kBatchSize = 4;
  constexpr auto kMaxWait = std::chrono::milliseconds(500);
  BatchCollector collector;

  BatchingQueue queue(kBatchSize, kMaxWait, kMaxPending, collector.handler());
  for (std::uint64_t id = 0; id < kBatchSize; ++id) {
    REQUIRE(queue.submit(Request{.id = id, .input = {static_cast<float>(id)}}));
  }

  // If this only fired via the max_wait timeout, it wouldn't arrive for
  // 500ms -- a generous fraction of that proves it fired on batch size.
  CHECK(
      collector.wait_for_at_least(kBatchSize, std::chrono::milliseconds(100)));
}

TEST_CASE("PartialBatchFlushesAfterMaxWait", "[batching_queue]") {
  constexpr std::size_t kBatchSize = 8;
  constexpr auto kMaxWait = std::chrono::milliseconds(20);
  BatchCollector collector;

  BatchingQueue queue(kBatchSize, kMaxWait, kMaxPending, collector.handler());
  REQUIRE(queue.submit(Request{.id = 1, .input = {1.0f}}));
  REQUIRE(queue.submit(Request{.id = 2, .input = {2.0f}}));

  // Only 2 of the required 8 requests were submitted, so this can only
  // succeed via the max_wait timeout path, not the batch-size path.
  REQUIRE(collector.wait_for_at_least(2, std::chrono::milliseconds(500)));
  auto batches = collector.snapshot();
  REQUIRE(batches.size() == 1u);
  CHECK(batches[0].size() == 2u);
}

TEST_CASE("DeliversRequestsToHandlerInSubmissionOrder", "[batching_queue]") {
  constexpr std::size_t kBatchSize = 3;
  constexpr std::size_t kTotal = kBatchSize * 2;
  constexpr auto kMaxWait = std::chrono::milliseconds(500);
  BatchCollector collector;

  BatchingQueue queue(kBatchSize, kMaxWait, kMaxPending, collector.handler());
  for (std::uint64_t id = 0; id < kTotal; ++id) {
    REQUIRE(queue.submit(Request{.id = id, .input = {static_cast<float>(id)}}));
  }

  REQUIRE(collector.wait_for_at_least(kTotal, std::chrono::milliseconds(500)));
  std::uint64_t expected_id = 0;
  for (const auto &batch : collector.snapshot()) {
    CHECK(batch.size() <= kBatchSize);
    for (const Request &req : batch) {
      CHECK(req.id == expected_id++);
    }
  }
  CHECK(expected_id == kTotal);
}

TEST_CASE("RejectsSubmitsBeyondMaxPending", "[batching_queue]") {
  // The cap is below the batch size and max_wait is long, so nothing leaves
  // pending_ while the test submits.
  constexpr std::size_t kBatchSize = 8;
  constexpr std::size_t kCap = 2;
  constexpr auto kMaxWait = std::chrono::milliseconds(500);
  BatchCollector collector;

  BatchingQueue queue(kBatchSize, kMaxWait, kCap, collector.handler());
  REQUIRE(queue.submit(Request{.id = 1, .input = {1.0f}}));
  REQUIRE(queue.submit(Request{.id = 2, .input = {2.0f}}));
  CHECK_FALSE(queue.submit(Request{.id = 3, .input = {3.0f}}));

  // Only the accepted requests are flushed once max_wait elapses.
  REQUIRE(collector.wait_for_at_least(kCap, std::chrono::seconds(2)));
  auto batches = collector.snapshot();
  REQUIRE(batches.size() == 1u);
  CHECK(batches[0].size() == kCap);
}
