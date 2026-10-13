#pragma once

#include <poll.h>

#include <functional>
#include <utility>

#include "loopnet/event_loop.h"

namespace loopnet {

class Channel {
public:
  using EventCallback = std::function<void()>;

  explicit Channel(EventLoop &loop, int fd) : loop_{loop}, fd_{fd} {}

  int fd() const { return fd_; }
  int events() const { return events_; }

  void set_revents(int revents) { revents_ = revents; }
  void set_read_callback(EventCallback cb) { read_callback_ = std::move(cb); };
  void set_write_callback(EventCallback cb) {
    write_callback_ = std::move(cb);
  };

  void enable_reading() {
    events_ |= POLLIN;
    update();
  }
  void enable_writing() {
    if (events_ & POLLOUT)
      return;
    events_ |= POLLOUT;
    update();
  }
  void disable_reading() {
    events_ &= ~POLLIN;
    update();
  }
  void disable_writing() {
    events_ &= ~POLLOUT;
    update();
  }

  void handle_event() {
    // errors and hangups surface through the read path (recv returns 0 or -1)
    if ((revents_ & (POLLIN | POLLERR | POLLHUP)) != 0 && read_callback_)
      read_callback_();
    if ((revents_ & POLLOUT) != 0 && write_callback_)
      write_callback_();
  }

private:
  void update() { loop_.update_channel(this); }

  EventLoop &loop_;
  int fd_;
  int events_ = 0, revents_ = 0;
  EventCallback read_callback_, write_callback_;
};

} // namespace loopnet
