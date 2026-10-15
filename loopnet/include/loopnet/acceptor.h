#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "loopnet/channel.h"
#include "loopnet/event_loop.h"

namespace loopnet {

class Acceptor {
public:
  using NewConnectionCallback = std::function<void(int client_fd)>;

  explicit Acceptor(EventLoop &loop, const std::string &bind_address,
                    std::uint16_t port);
  ~Acceptor();
  Acceptor(const Acceptor &) = delete;
  Acceptor &operator=(const Acceptor &) = delete;

  void set_new_connection_callback(NewConnectionCallback cb) {
    cb_ = std::move(cb);
  };

  void start() {
    channel_.set_read_callback([this] { handle_read(); });
    channel_.enable_reading();
  }

private:
  void handle_read();

  EventLoop &loop_;
  int listener_fd_;
  // spare fd released on EMFILE/ENFILE so pending connections can be shed
  int idle_fd_;
  Channel channel_;
  NewConnectionCallback cb_;
};

} // namespace loopnet
