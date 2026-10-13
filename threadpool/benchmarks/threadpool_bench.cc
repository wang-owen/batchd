#include "threadpool/thread_pool.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

constexpr int kTasks = 100'000;
constexpr int kHeavyTasks = 1'000;
constexpr int kItersPerHeavyTask = 10'000;

int run_serial(int task_count) {
  std::atomic<int> counter{0};
  for (int i = 0; i < task_count; ++i) {
    counter.fetch_add(1, std::memory_order_relaxed);
  }
  return counter.load();
}

// Includes pool startup and teardown: the destructor joins every worker, which
// is the only way to wait for all submitted tasks to finish.
int run_pooled(int task_count, std::size_t worker_count) {
  std::atomic<int> counter{0};
  {
    threadpool::ThreadPool pool(worker_count);
    for (int i = 0; i < task_count; ++i) {
      pool.submit(
          [&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
  }
  return counter.load();
}

// Roughly 20 us of dependent xorshift steps; the optimizer can't fold the loop
// away because each step depends on the previous one.
std::uint64_t busy_work(std::uint64_t seed) {
  std::uint64_t x = seed | 1;
  for (int i = 0; i < kItersPerHeavyTask; ++i) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
  }
  return x;
}

std::uint64_t run_serial_heavy(int task_count) {
  std::uint64_t sum = 0;
  for (int i = 0; i < task_count; ++i) {
    sum += busy_work(static_cast<std::uint64_t>(i));
  }
  return sum;
}

std::uint64_t run_pooled_heavy(int task_count, std::size_t worker_count) {
  std::atomic<std::uint64_t> sum{0};
  {
    threadpool::ThreadPool pool(worker_count);
    for (int i = 0; i < task_count; ++i) {
      pool.submit([&sum, i] {
        sum.fetch_add(busy_work(static_cast<std::uint64_t>(i)),
                      std::memory_order_relaxed);
      });
    }
  }
  return sum.load();
}

} // namespace

TEST_CASE("100k trivial tasks", "[!benchmark][threadpool]") {
  const unsigned hw_threads = std::max(2u, std::thread::hardware_concurrency());

  BENCHMARK("1 thread (serial)") { return run_serial(kTasks); };

  BENCHMARK(std::to_string(hw_threads) + " worker ThreadPool") {
    return run_pooled(kTasks, hw_threads);
  };
}

TEST_CASE("1k ~20us tasks", "[!benchmark][threadpool]") {
  const unsigned hw_threads = std::max(2u, std::thread::hardware_concurrency());

  BENCHMARK("1 thread (serial)") { return run_serial_heavy(kHeavyTasks); };

  BENCHMARK(std::to_string(hw_threads) + " worker ThreadPool") {
    return run_pooled_heavy(kHeavyTasks, hw_threads);
  };
}
