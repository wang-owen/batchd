#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "inference_srv/request.h"

namespace inference_srv {

using Clock = std::chrono::steady_clock;

using BatchHandler = std::function<void(std::vector<Request> batch)>;

// Accumulates submitted requests until either `max_batch_size` requests
// have arrived or `max_wait` has elapsed since the first request in the
// current batch, then hands the whole batch to `handler` on the worker
// thread. Producing and delivering responses is up to the handler.
// At most `max_pending` requests wait at once; submit() rejects the rest.
class BatchingQueue {
public:
  BatchingQueue(std::size_t max_batch_size, std::chrono::milliseconds max_wait,
                std::size_t max_pending, BatchHandler handler);
  ~BatchingQueue();
  BatchingQueue(const BatchingQueue &) = delete;
  BatchingQueue &operator=(const BatchingQueue &) = delete;

  // Returns false, dropping `request`, if `max_pending` requests are
  // already waiting.
  [[nodiscard]] bool submit(Request request);

  std::uint64_t get_id() { return counter_++; }

private:
  struct PendingRequest {
    Request request;
    std::chrono::time_point<Clock> enqueue_time = Clock::now();
  };

  void worker_loop();

  std::size_t max_batch_size_;
  std::chrono::milliseconds max_wait_;
  std::size_t max_pending_;
  BatchHandler handler_;

  std::mutex mtx_;
  std::condition_variable cv_;
  std::deque<PendingRequest> pending_;
  std::atomic<std::uint64_t> counter_ = 0;
  bool stop_ = false;
  std::thread worker_thread_;
};

} // namespace inference_srv
