#include "loopnet/acceptor.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

#include "loopnet/fd_util.h"

namespace loopnet {

namespace {

[[noreturn]] void close_and_throw(int fd, const char *what) {
  int err = errno;
  ::close(fd);
  throw std::system_error(err, std::system_category(), what);
}

int create_listener(const std::string &bind_address, std::uint16_t port) {
  sockaddr_in server_address{};
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);
  if (bind_address.empty()) {
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
  } else if (::inet_pton(AF_INET, bind_address.c_str(),
                         &server_address.sin_addr) != 1) {
    throw std::invalid_argument("invalid IPv4 bind address: " + bind_address);
  }

  int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listener == -1)
    throw std::system_error(errno, std::system_category(), "::socket");

  if (::fcntl(listener, F_SETFD, FD_CLOEXEC) == -1)
    close_and_throw(listener, "::fcntl");

  try {
    fd_util::set_nonblocking(listener);
  } catch (...) {
    ::close(listener);
    throw;
  }

  int one = 1;
  if (::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1)
    close_and_throw(listener, "::setsockopt");

  if (::bind(listener, reinterpret_cast<sockaddr *>(&server_address),
             sizeof(server_address)) == -1)
    close_and_throw(listener, "::bind");

  if (::listen(listener, SOMAXCONN) == -1)
    close_and_throw(listener, "::listen");

  return listener;
}

} // namespace

Acceptor::Acceptor(EventLoop &loop, const std::string &bind_address,
                   std::uint16_t port)
    : loop_{loop}, listener_fd_{create_listener(bind_address, port)},
      channel_{loop, listener_fd_} {
  idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  if (idle_fd_ == -1)
    close_and_throw(listener_fd_, "::open");
}

Acceptor::~Acceptor() {
  loop_.erase_channel(&channel_);
  ::close(listener_fd_);
  ::close(idle_fd_);
}

void Acceptor::handle_read() {
  while (true) {
    int client_fd = ::accept(listener_fd_, nullptr, nullptr);

    if (client_fd != -1) {
      // TODO: change to fd_util::set_nonblocking without try-catch
      if (::fcntl(client_fd, F_SETFD, FD_CLOEXEC) == -1 ||
          ::fcntl(client_fd, F_SETFL,
                  ::fcntl(client_fd, F_GETFL) | O_NONBLOCK) == -1) {
        ::close(client_fd);
        continue;
      }
      cb_(client_fd);
      continue;
    }

    switch (errno) {
    case EINTR:
    case ECONNABORTED:
    case EPROTO:
      // The listener itself is still healthy. Try again.
      continue;
    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
#endif
      // Nothing left to accept right now; hand control back to the loop.
      return;
    case EMFILE:
    case ENFILE:
      // Out of fds. The pending connection keeps the listener readable, so
      // returning would make poll() spin. Free the spare fd, accept and drop
      // the connection, then reclaim the spare.
      ::close(idle_fd_);
      idle_fd_ = ::accept(listener_fd_, nullptr, nullptr);
      if (idle_fd_ != -1)
        ::close(idle_fd_);
      idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
      if (idle_fd_ == -1)
        return;
      continue;
    default:
      perror("::accept");
      return;
    }
  }
}

} // namespace loopnet
