#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace loopnet {

class Channel;
class Poller;

class EventLoop {
public:
  using Task = std::function<void()>;

  EventLoop();
  ~EventLoop();
  EventLoop(const EventLoop &) = delete;
  EventLoop &operator=(const EventLoop &) = delete;

  void start();
  void stop();

  void update_channel(Channel *channel);
  void erase_channel(Channel *channel);

  void queue_task(Task t);

  void wakeup();

private:
  std::atomic<bool> quit_ = false;
  int wakeup_pipe_[2];
  std::unique_ptr<Channel> wakeup_channel_;

  std::unique_ptr<Poller> poller_;
  std::vector<Channel *> active_channels_;
  std::vector<Task> pending_tasks_;
  std::mutex mtx_;
};

} // namespace loopnet
