#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

#include "loopnet/acceptor.h"
#include "loopnet/callbacks.h"

namespace loopnet {

class EventLoop;

class TcpServer {
public:
  explicit TcpServer(EventLoop &loop, const std::string &bind_address,
                     std::uint16_t port);
  ~TcpServer();
  TcpServer(const TcpServer &) = delete;
  TcpServer &operator=(const TcpServer &) = delete;

  void start() { acceptor_.start(); }

  void set_message_callback(MessageCallback cb) {
    message_callback_ = std::move(cb);
  }

private:
  void handle_new_connection(int client_fd);

  EventLoop &loop_;
  Acceptor acceptor_;
  std::unordered_map<int, TcpConnectionPtr> connections_;
  MessageCallback message_callback_;
};

} // namespace loopnet
