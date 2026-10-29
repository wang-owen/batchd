#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

#include "allocator/arena_allocator.h"
#include "batchd/request.h"

namespace batchd {

// Stub "model": no real inference
class InferenceEngine {
public:
  InferenceEngine();

  void run_batch(const std::vector<Request> &batch,
                 std::vector<Response> &out_responses);

  // Simulated model latency of one run_batch() call: a fixed per-call cost
  // plus a per-item cost.
  static std::chrono::milliseconds batch_latency(std::size_t batch_size);

private:
  allocator::ArenaAllocator batch_arena_;
};

} // namespace batchd
