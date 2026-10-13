#include "loopnet/event_loop.h"

#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include "loopnet/fd_util.h"
#include "loopnet/poller.h"

namespace loopnet {

EventLoop::EventLoop() {
  poller_ = std::make_unique<Poller>();

  if (::pipe(wakeup_pipe_) == -1)
    throw std::system_error(errno, std::system_category(), "::pipe");

  fd_util::set_nonblocking(wakeup_pipe_[0]);
  fd_util::set_nonblocking(wakeup_pipe_[1]);
  wakeup_channel_ = std::make_unique<Channel>(*this, wakeup_pipe_[0]);
  wakeup_channel_->enable_reading();
  wakeup_channel_->set_read_callback([this] {
    // drain pipe
    char buffer[64];
    while (::read(wakeup_pipe_[0], buffer, sizeof(buffer)) > 0)
      ;
  });
}

EventLoop::~EventLoop() {
  ::close(wakeup_pipe_[0]);
  ::close(wakeup_pipe_[1]);
}

void EventLoop::start() {
  while (!quit_.load(std::memory_order_acquire)) {
    active_channels_.clear();

    // infinite poll sleep; woken up by wakeup pipe
    if (poller_->poll(-1, active_channels_) == -1)
      throw std::system_error(errno, std::system_category(), "::poll");

    for (Channel *channel : active_channels_)
      channel->handle_event();

    std::vector<Task> tasks;
    {
      std::lock_guard<std::mutex> lock{mtx_};
      std::swap(tasks, pending_tasks_);
    }
    for (Task &task : tasks)
      task();
  }
}

void EventLoop::stop() {
  quit_.store(true, std::memory_order_release);
  wakeup();
}

void EventLoop::update_channel(Channel *channel) {
  poller_->update_channel(channel);
}

void EventLoop::erase_channel(Channel *channel) {
  poller_->erase_channel(channel);
}

void EventLoop::queue_task(Task t) {
  bool was_empty;
  {
    std::lock_guard<std::mutex> lock{mtx_};
    was_empty = pending_tasks_.empty();
    pending_tasks_.push_back(std::move(t));
  }
  if (was_empty)
    wakeup();
}

void EventLoop::wakeup() {
  char one = 1;
  (void)::write(wakeup_pipe_[1], &one, 1);
}

} // namespace loopnet
