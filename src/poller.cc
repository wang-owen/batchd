#include "loopnet/poller.h"

#include <poll.h>

#include <algorithm>
#include <cassert>
#include <cerrno>

namespace loopnet {

int Poller::poll(int timeout_ms, ChannelList &active) {
  int num_ready;
  do {
    num_ready = ::poll(pollfds_.data(), pollfds_.size(), timeout_ms);
  } while (num_ready == -1 && (errno == EAGAIN || errno == EINTR));

  if (num_ready == -1)
    return -1;

  for (const pollfd &pfd : pollfds_) {
    if (pfd.revents == 0)
      continue;

    Channel *channel = channels_.at(pfd.fd);
    channel->set_revents(pfd.revents);
    active.push_back(channel);
  }

  return num_ready;
}

void Poller::update_channel(Channel *channel) {
  assert(channel != nullptr);

  int channel_fd = channel->fd();
  channels_[channel_fd] = channel;

  auto it = std::ranges::find(pollfds_, channel_fd, &pollfd::fd);
  if (it != pollfds_.cend()) {
    it->events = channel->events();
  } else {
    pollfds_.emplace_back(channel_fd, channel->events());
  }
}

void Poller::erase_channel(Channel *channel) {
  assert(channel != nullptr);
  assert(channels_.contains(channel->fd()));

  int channel_fd = channel->fd();
  channels_.erase(channel_fd);
  auto it = std::ranges::find(pollfds_, channel_fd, &pollfd::fd);
  pollfds_.erase(it);
}

} // namespace loopnet
