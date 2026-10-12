#include <cstdint>
#include <format>
#include <iostream>
#include <string>

#include "loopnet/buffer.h"
#include "loopnet/callbacks.h"
#include "loopnet/event_loop.h"
#include "loopnet/tcp_connection.h" // IWYU pragma: keep
#include "loopnet/tcp_server.h"

int main() {
  constexpr std::string kBindAddress = "127.0.0.1";
  constexpr std::uint16_t kPort = 9090;

  loopnet::EventLoop loop;
  loopnet::TcpServer server{loop, kBindAddress, kPort};

  server.set_message_callback(
      [](loopnet::TcpConnectionPtr ptr, loopnet::Buffer &buffer) {
        std::cout << std::format("Client fd {} msg: {}\n", ptr->channel().fd(),
                                 buffer.retrieve(buffer.readable_bytes()));
      });

  server.start();
  loop.start();
}
