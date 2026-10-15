#include "loopnet/tcp_server.h"

#include <memory>
#include <utility>

#include "loopnet/buffer.h"
#include "loopnet/event_loop.h"
#include "loopnet/tcp_connection.h"

namespace loopnet {

TcpServer::TcpServer(EventLoop &loop, const std::string &bind_address,
                     std::uint16_t port)
    : loop_{loop}, acceptor_{loop, bind_address, port} {
  acceptor_.set_new_connection_callback(
      [this](int client_fd) { handle_new_connection(client_fd); });
}

TcpServer::~TcpServer() {
  for (auto &[fd, conn] : connections_) {
    loop_.erase_channel(&conn->channel());
  }
}

void TcpServer::handle_new_connection(int client_fd) {
  TcpConnectionPtr connection =
      std::make_shared<TcpConnection>(loop_, client_fd);

  connection->set_message_callback(
      [this](const TcpConnectionPtr &conn, Buffer &buffer) {
        message_callback_(conn, buffer);
      });
  connection->set_close_callback([this](const TcpConnectionPtr &conn) {
    auto node = connections_.extract(conn->channel().fd());
    if (node.empty()) {
      return;
    }

    loop_.erase_channel(&conn->channel());
    // task holds last reference to node
    loop_.queue_task([conn = std::move(node.mapped())] {});
  });

  connection->channel().enable_reading();
  connections_.emplace(client_fd, connection);
}

} // namespace loopnet
