#include "batchd/batchd.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <optional>
#include <ranges>
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "loopnet/buffer.h"
#include "loopnet/callbacks.h"
#include "loopnet/event_loop.h"
#include "loopnet/tcp_connection.h" // IWYU pragma: keep
#include "loopnet/tcp_server.h"
#include "threadpool/thread_pool.h"

namespace {

// Releases one slot upon destruction
struct SlotRelease {
  explicit SlotRelease(std::counting_semaphore<> &sem) : sem{sem} {}
  ~SlotRelease() { sem.release(); }
  SlotRelease(const SlotRelease &) = delete;
  SlotRelease &operator=(const SlotRelease &) = delete;

  std::counting_semaphore<> &sem;
};

std::string response_to_string(const batchd::Response &response) {
  std::string msg = std::format("OK {}", response.client_id);
  for (float f : response.output) {
    msg += std::format(" {}", f);
  }
  return msg + '\n';
}

std::string_view second_token(std::string_view line) {
  auto tokens = line | std::views::split(' ') |
                std::views::filter([](auto &&r) { return !r.empty(); });
  auto it = std::ranges::next(tokens.begin(), 1, tokens.end());
  if (it == tokens.end()) {
    return {};
  }
  return std::string_view((*it).begin(), (*it).end());
}

std::string err_reply(std::string_view client_id_token,
                      std::string_view reason) {
  std::uint64_t client_id = 0;
  const char *last = client_id_token.data() + client_id_token.size();
  auto [end, ec] = std::from_chars(client_id_token.data(), last, client_id);
  if (client_id_token.empty() || ec != std::errc{} || end != last) {
    return std::format("ERR - {}\n", reason);
  }
  return std::format("ERR {} {}\n", client_id, reason);
}

} // namespace

int main() {
  // TODO: graceful shutdown that drains pending/in-flight requests and replies
  // to clients instead of dropping them.
  std::signal(SIGINT, [](int) { _exit(0); });

  constexpr std::string_view kBindAddress = "127.0.0.1";
  constexpr std::uint16_t kPort = 9091;
  constexpr std::size_t kMaxBatchSize = 4;
  // Cap on pending requests, in rounds of one full batch per worker. Each
  // round takes about one batch latency to drain, so under overload a full
  // queue adds roughly kBacklogRounds * batch_latency(kMaxBatchSize) of wait.
  constexpr std::size_t kBacklogRounds = 5;
  constexpr std::size_t kMaxLineBytes = 64 * 1024;
  constexpr std::chrono::milliseconds kMaxWaitMs = std::chrono::milliseconds(5);

  // Prevent writing to a disconnected client from killing server
  std::signal(SIGPIPE, SIG_IGN);

  loopnet::EventLoop loop;

  unsigned num_workers = std::max(2u, std::thread::hardware_concurrency());
  std::counting_semaphore<> in_flight_slots{num_workers};
  threadpool::ThreadPool pool(num_workers);
  const std::size_t max_pending = num_workers * kMaxBatchSize * kBacklogRounds;

  batchd::BatchingQueue batching_queue(
      kMaxBatchSize, kMaxWaitMs, max_pending,
      [&loop, &pool, &in_flight_slots](std::vector<batchd::Request> batch) {
        // Blocks the batching thread until a worker is free, so the backlog
        // waits in BatchingQueue (FIFO) instead of the pool's deques (LIFO).
        in_flight_slots.acquire();
        pool.submit([&loop, &in_flight_slots, batch = std::move(batch)] {
          SlotRelease slot{in_flight_slots};
          thread_local batchd::InferenceEngine engine;
          std::vector<batchd::Response> responses;
          try {
            engine.run_batch(batch, responses);
          } catch (const std::exception &) {
            for (const batchd::Request &request : batch) {
              loop.queue_task([conn = request.conn,
                               client_id = request.client_id] {
                conn->send(std::format("ERR {} engine failure\n", client_id));
              });
            }
            return;
          }

          for (batchd::Response &response : responses) {
            loop.queue_task([response = std::move(response)] {
              response.conn->send(response_to_string(response));
            });
          }
        });
      });

  std::optional<loopnet::TcpServer> server;
  try {
    server.emplace(loop, std::string(kBindAddress), kPort);
  } catch (const std::exception &e) {
    std::cerr << std::format("Failed to listen on {}:{}: {}\n", kBindAddress,
                             kPort, e.what());
    return 1;
  }

  // Runs on the loop thread each time bytes arrive on a connection; `buffer`
  // holds everything received on it that hasn't been consumed yet.
  server->set_message_callback([&](const loopnet::TcpConnectionPtr &conn,
                                   loopnet::Buffer &buffer) {
    //   request:  INFER <client_id> <f1> <f2> ... <fn>\n
    //   response: OK <client_id> <o1> ... <on>\n
    //         or: ERR <client_id> <reason>\n   ("-" if no valid client_id)

    while (true) {
      std::string_view text = buffer.peek();
      std::size_t len = text.find('\n');
      if (len == std::string::npos) {
        break;
      }
      std::string_view line = text.substr(0, len);
      if (len > kMaxLineBytes) {
        std::string reply = err_reply(second_token(line), "request too large");
        buffer.consume(len + 1);
        conn->send(reply);
        continue;
      }

      auto tokens = (line | std::views::split(' ') |
                     std::views::filter([](auto &&r) { return !r.empty(); }) |
                     std::views::transform([](auto &&r) {
                       return std::string(r.begin(), r.end());
                     })) |
                    std::ranges::to<std::vector>();

      buffer.consume(len + 1);

      std::string_view client_id_token =
          tokens.size() > 1 ? std::string_view(tokens[1]) : std::string_view();
      if (tokens.size() <= 2 || tokens[0] != "INFER") {
        conn->send(err_reply(client_id_token, "invalid request"));
        continue;
      }

      try {
        batchd::Request request{.id = batching_queue.get_id(),
                                .client_id = std::stoull(tokens[1]),
                                .conn = conn};
        for (std::size_t i = 2; i < tokens.size(); ++i) {
          request.input.push_back(std::stof(tokens[i]));
        }

        if (!batching_queue.submit(std::move(request))) {
          conn->send(err_reply(client_id_token, "busy"));
        }
      } catch (const std::exception &) {
        conn->send(err_reply(client_id_token, "invalid request"));
        continue;
      }
    }
    if (buffer.readable_bytes() > kMaxLineBytes) {
      std::string reply =
          err_reply(second_token(buffer.peek()), "request too large");
      buffer.consume(buffer.readable_bytes());
      conn->send(reply);
    }
  });

  server->start();
  loop.start();
}
