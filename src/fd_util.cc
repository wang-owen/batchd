#include "loopnet/fd_util.h"

#include <fcntl.h>

#include <cerrno>
#include <system_error>

namespace loopnet {

namespace fd_util {

void set_blocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL);
  if (flags == -1 || ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK)) {
    throw std::system_error(errno, std::system_category(), "::fcntl");
  }
}

void set_nonblocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL);
  if (flags == -1 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
    throw std::system_error(errno, std::system_category(), "::fcntl");
  }
}

} // namespace fd_util

} // namespace loopnet
