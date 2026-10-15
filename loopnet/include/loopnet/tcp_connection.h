#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

#include "loopnet/buffer.h"
#include "loopnet/callbacks.h"
#include "loopnet/channel.h"
#include "loopnet/event_loop.h"

namespace loopnet {

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
  explicit TcpConnection(EventLoop &loop, int client_fd);
  ~TcpConnection();
  TcpConnection(const TcpConnection &) = delete;
  TcpConnection &operator=(const TcpConnection &) = delete;

  void set_message_callback(MessageCallback cb) {
    message_callback_ = std::move(cb);
  }
  void set_close_callback(CloseCallback cb) { close_callback_ = std::move(cb); }

  void send(std::string_view msg);

  Channel &channel() { return channel_; }

private:
  inline constexpr static std::size_t kBufferSize = 65536;

  void handle_read();
  void handle_write();
  void handle_close();

  Channel channel_;
  MessageCallback message_callback_;
  CloseCallback close_callback_;
  Buffer in_buffer_, out_buffer_;
  bool closed_ = false;
  bool read_closed_ = false;
};

} // namespace loopnet
