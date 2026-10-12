#pragma once

#include <functional>
#include <memory>

#include "loopnet/buffer.h"

namespace loopnet {

class TcpConnection;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using MessageCallback = std::function<void(const TcpConnectionPtr &, Buffer &)>;
using CloseCallback = std::function<void(const TcpConnectionPtr &)>;

} // namespace loopnet
