#include "minicache/minicache.h"

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "loopnet/buffer.h"
#include "loopnet/callbacks.h"
#include "loopnet/event_loop.h"
#include "loopnet/tcp_connection.h" // IWYU pragma: keep
#include "loopnet/tcp_server.h"

namespace {

std::atomic<loopnet::EventLoop *> gLoop{nullptr};

void handle_shutdown_signal(int) {
  if (loopnet::EventLoop *loop = gLoop.load()) {
    loop->stop();
  }
}

} // namespace

int main() {
  constexpr std::string_view kBindAddress = "127.0.0.1";
  constexpr std::uint16_t kPort = 9090;

  minicache::LruCache cache(1024);
  minicache::CommandDispatcher dispatcher(cache);

  loopnet::EventLoop loop;
  std::optional<loopnet::TcpServer> server;
  try {
    server.emplace(loop, std::string(kBindAddress), kPort);
  } catch (const std::exception &e) {
    std::cerr << std::format("Failed to start server at {}:{}: {}\n",
                             kBindAddress, kPort, e.what());
    return 1;
  }

  server->set_message_callback([&dispatcher](
                                   const loopnet::TcpConnectionPtr &conn,
                                   loopnet::Buffer &buffer) {
    std::size_t newline_pos;
    while ((newline_pos = buffer.peek().find('\n')) != std::string_view::npos) {
      std::string command{buffer.peek().substr(0, newline_pos)};
      buffer.consume(newline_pos + 1);
      conn->send(dispatcher.dispatch(command));
    }
  });

  gLoop.store(&loop);
  std::signal(SIGINT, handle_shutdown_signal);
  std::cout << std::format("minicache: listening on {}:{}\n", kBindAddress,
                           kPort);
  server->start();
  loop.start();
}
