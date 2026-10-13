#include "inference_srv/inference_srv.h"

#include <algorithm>
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

std::string response_to_string(const inference_srv::Response &response) {
  std::string msg = std::format("OK {}", response.client_id);
  for (float f : response.output) {
    msg += std::format(" {}", f);
  }
  return msg + '\n';
}

} // namespace

int main() {
  // TODO: graceful shutdown that drains pending/in-flight requests and replies
  // to clients instead of dropping them.
  std::signal(SIGINT, [](int) { _exit(0); });

  constexpr std::string_view kBindAddress = "127.0.0.1";
  constexpr std::uint16_t kPort = 9091;
  constexpr std::size_t kMaxBatchSize = 4;
  constexpr std::size_t kMaxLineBytes = 64 * 1024;
  constexpr std::chrono::milliseconds kMaxWaitMs =
      std::chrono::milliseconds(50);

  // Prevent writing to a disconnected client from killing server
  std::signal(SIGPIPE, SIG_IGN);

  loopnet::EventLoop loop;
  threadpool::ThreadPool pool(
      std::max(2u, std::thread::hardware_concurrency()));
  inference_srv::BatchingQueue batching_queue(
      kMaxBatchSize, kMaxWaitMs,
      [&loop, &pool](std::vector<inference_srv::Request> batch) {
        // PERF: no cap on batches in flight; under overload the pool's queues
        // grow without bound and batches flush half-empty.
        pool.submit([&loop, batch = std::move(batch)] {
          thread_local inference_srv::InferenceEngine engine;
          std::vector<inference_srv::Response> responses;
          try {
            engine.run_batch(batch, responses);
          } catch (const std::exception &) {
            for (const inference_srv::Request &request : batch) {
              loop.queue_task([conn = request.conn] {
                conn->send("ERR engine failure\n");
              });
            }
            return;
          }

          for (inference_srv::Response &response : responses) {
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
    //   response:    OK <client_id> <o1> ... <on>\n   or   ERR <reason>\n

    while (true) {
      std::string_view text = buffer.peek();
      std::size_t len = text.find('\n');
      if (len == std::string::npos) {
        break;
      }
      if (len > kMaxLineBytes) {
        buffer.consume(len + 1);
        conn->send("ERR request too large\n");
        continue;
      }

      auto tokens = (text.substr(0, len) | std::views::split(' ') |
                     std::views::filter([](auto &&r) { return !r.empty(); }) |
                     std::views::transform([](auto &&r) {
                       return std::string(r.begin(), r.end());
                     })) |
                    std::ranges::to<std::vector>();

      buffer.consume(len + 1);

      if (tokens.size() <= 2 || tokens[0] != "INFER") {
        conn->send("ERR invalid request\n");
        continue;
      }

      try {
        inference_srv::Request request{.id = batching_queue.get_id(),
                                       .client_id = std::stoull(tokens[1]),
                                       .conn = conn};
        for (std::size_t i = 2; i < tokens.size(); ++i) {
          request.input.push_back(std::stof(tokens[i]));
        }
        batching_queue.submit(std::move(request));
      } catch (const std::exception &) {
        conn->send("ERR invalid request\n");
        continue;
      }
    }
    if (buffer.readable_bytes() > kMaxLineBytes) {
      buffer.consume(buffer.readable_bytes());
      conn->send("ERR request too large\n");
    }
  });

  server->start();
  loop.start();
}
