#include "inference_srv/batching_queue.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <thread>
#include <utility>

namespace inference_srv {

BatchingQueue::BatchingQueue(std::size_t max_batch_size,
                             std::chrono::milliseconds max_wait,
                             std::size_t max_pending, BatchHandler handler)
    : max_batch_size_(max_batch_size), max_wait_(max_wait),
      max_pending_(max_pending), handler_(std::move(handler)) {
  worker_thread_ = std::thread{[this] { worker_loop(); }};
}

BatchingQueue::~BatchingQueue() {
  {
    std::lock_guard<std::mutex> lock{mtx_};
    stop_ = true;
    cv_.notify_all();
  }
  worker_thread_.join();
}

bool BatchingQueue::submit(Request request) {
  std::lock_guard<std::mutex> lock{mtx_};
  if (pending_.size() >= max_pending_) {
    return false;
  }
  pending_.emplace_back(std::move(request));
  cv_.notify_one();
  return true;
}

void BatchingQueue::worker_loop() {
  while (true) {
    std::vector<PendingRequest> pending_requests;
    std::size_t len;
    {
      std::unique_lock<std::mutex> lock{mtx_};
      // BUG: pending_ requests dropped on shutdown without invoking callback;
      // caller hangs
      cv_.wait(lock, [this] { return stop_ || !pending_.empty(); });
      if (stop_) {
        break;
      }

      auto deadline = pending_.front().enqueue_time + max_wait_;
      cv_.wait_until(lock, deadline, [this] {
        return stop_ || pending_.size() >= max_batch_size_;
      });
      if (stop_) {
        break;
      }

      len =
          std::min(pending_.size(), static_cast<std::size_t>(max_batch_size_));

      for (std::size_t i = 0; i < len; ++i) {
        pending_requests.push_back(std::move(pending_[i]));
      }
      pending_.erase(pending_.begin(), pending_.begin() + len);
    }

    pending_requests.reserve(len);
    std::vector<Request> batch;
    batch.reserve(len);
    for (std::size_t i = 0; i < len; ++i) {
      batch.push_back(std::move(pending_requests[i].request));
    }

    handler_(std::move(batch));
  }
}

} // namespace inference_srv
