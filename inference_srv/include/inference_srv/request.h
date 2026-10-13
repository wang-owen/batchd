#pragma once

#include <cstdint>
#include <vector>

#include "loopnet/callbacks.h"

namespace inference_srv {

struct Request {
  std::uint64_t id;
  std::uint64_t client_id;
  loopnet::TcpConnectionPtr conn;
  std::vector<float> input;
};

struct Response {
  std::uint64_t id;
  std::uint64_t client_id;
  loopnet::TcpConnectionPtr conn;
  std::vector<float> output;
};

} // namespace inference_srv
