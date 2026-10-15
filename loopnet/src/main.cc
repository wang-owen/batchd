#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

#include "loopnet/buffer.h"
#include "loopnet/callbacks.h"
#include "loopnet/event_loop.h"
#include "loopnet/tcp_connection.h" // IWYU pragma: keep
#include "loopnet/tcp_server.h"

int main() {
  constexpr std::string_view kBindAddress = "127.0.0.1";
  constexpr std::uint16_t kPort = 9090;

  loopnet::EventLoop loop;
  loopnet::TcpServer server{loop, std::string(kBindAddress), kPort};

  server.set_message_callback(
      [](loopnet::TcpConnectionPtr ptr, loopnet::Buffer &buffer) {
        std::cout << std::format("Client fd {} msg: {}\n", ptr->channel().fd(),
                                 buffer.retrieve(buffer.readable_bytes()));
        ptr->send("ACK");
      });

  server.start();
  loop.start();
}
