#pragma once

#include <tl/expected.hpp>

#include <string>

namespace foxglove {

enum class FoxgloveError {
  None = 0,
  InvalidArgument = 1,
  ChannelClosed = 2,
  ServerStartFailed = 3,
  IoError = 4,
  SerializationError = 5,
  ProtocolError = 6,
};

template <typename T>
using FoxgloveResult = tl::expected<T, FoxgloveError>;

std::string foxglove_error_string(FoxgloveError error);

}  // namespace foxglove

#define FOXGLOVE_TRY(var, expr)                                      \
  auto _foxglove_try_##var = (expr);                                 \
  if (!_foxglove_try_##var.has_value()) {                            \
    return tl::make_unexpected(_foxglove_try_##var.error());         \
  }                                                                  \
  auto var = std::move(_foxglove_try_##var.value())
