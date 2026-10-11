#include "loopnet/buffer.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace loopnet {

void Buffer::append(std::string_view data) {
  ensure_writable(data.size());
  std::ranges::copy(data, buffer_.begin() + write_);
  write_ += data.size();
}

std::string Buffer::retrieve(std::size_t len) {
  len = std::min(len, readable_bytes());
  std::string data{buffer_.begin() + read_, buffer_.begin() + read_ + len};
  read_ += len;
  if (read_ == write_) {
    read_ = 0;
    write_ = 0;
  }
  return data;
}

void Buffer::consume(std::size_t len) {
  len = std::min(len, readable_bytes());
  read_ += len;
  if (read_ == write_) {
    read_ = 0;
    write_ = 0;
  }
}

void Buffer::ensure_writable(std::size_t len) {
  if (writable_bytes() >= len)
    return;
  if (read_ + writable_bytes() >= len) {
    // compact: move readable bytes to the front
    std::copy(buffer_.begin() + read_, buffer_.begin() + write_,
              buffer_.begin());
    write_ -= read_;
    read_ = 0;
  } else {
    buffer_.resize(write_ + len);
    // expose the vector's geometric slack as writable space
    buffer_.resize(buffer_.capacity());
  }
}

} // namespace loopnet
