#include "loopnet/tcp_connection.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <string_view>

#include "loopnet/event_loop.h"

namespace loopnet {

TcpConnection::TcpConnection(EventLoop &loop, int client_fd)
    : channel_{loop, client_fd} {
  channel_.set_read_callback([this] { handle_read(); });
  channel_.set_write_callback([this] { handle_write(); });
}

TcpConnection::~TcpConnection() { ::close(channel_.fd()); }

void TcpConnection::send(std::string_view msg) {
  if (closed_)
    return;
  out_buffer_.append(msg);
  channel_.enable_writing();
}

void TcpConnection::handle_read() {
  if (closed_)
    return;
  char buffer[kBufferSize];
  ssize_t bytes_received = ::recv(channel_.fd(), buffer, sizeof(buffer), 0);
  if (bytes_received > 0) {
    in_buffer_.append({buffer, static_cast<std::size_t>(bytes_received)});
    message_callback_(shared_from_this(), in_buffer_);
  } else if (bytes_received == 0 || (errno != EAGAIN && errno != EINTR)) {
    closed_ = true;
    close_callback_(shared_from_this());
  }
}

void TcpConnection::handle_write() {
  if (closed_)
    return;
  std::string_view data = out_buffer_.peek();
  // sending to closed peer raises SIGINT; MSG_NOSIGNAL has it return -1 instead
  ssize_t bytes_sent =
      ::send(channel_.fd(), data.data(), data.size(), MSG_NOSIGNAL);
  if (bytes_sent >= 0) {
    out_buffer_.consume(bytes_sent);
  } else {
    if (errno != EAGAIN && errno != EINTR) {
      closed_ = true;
      close_callback_(shared_from_this());
    }
    return;
  }
  if (out_buffer_.empty())
    channel_.disable_writing();
}

} // namespace loopnet
