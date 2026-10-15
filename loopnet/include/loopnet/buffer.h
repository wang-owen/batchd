#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace loopnet {

class Buffer {
public:
  void append(std::string_view data);
  std::string retrieve(std::size_t len);
  void consume(std::size_t len);

  std::string_view peek() const {
    return std::string_view{buffer_.cbegin() + read_,
                            buffer_.cbegin() + write_};
  }

  std::size_t readable_bytes() const { return write_ - read_; }
  std::size_t writable_bytes() const { return buffer_.size() - write_; }

  bool empty() const { return read_ == write_; }

private:
  void ensure_writable(std::size_t len);

  std::vector<char> buffer_;
  std::size_t read_ = 0, write_ = 0;
};

} // namespace loopnet
