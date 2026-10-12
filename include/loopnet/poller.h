#pragma once

#include <poll.h>

#include <unordered_map>
#include <vector>

#include "loopnet/channel.h"

namespace loopnet {

class Poller {
public:
  using ChannelList = std::vector<Channel *>;

  Poller() = default;
  Poller(const Poller &) = delete;
  Poller &operator=(const Poller &) = delete;

  int poll(int timeout_ms, ChannelList &active);

  void update_channel(Channel *channel);
  void erase_channel(Channel *channel);

private:
  std::vector<pollfd> pollfds_;
  std::unordered_map<int, Channel *> channels_; // fd -> channel
};

} // namespace loopnet
