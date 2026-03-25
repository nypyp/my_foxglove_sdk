#include <foxglove/error.hpp>

namespace foxglove {

std::string foxglove_error_string(FoxgloveError error) {
  switch (error) {
    case FoxgloveError::None:
      return "no error";
    case FoxgloveError::InvalidArgument:
      return "invalid argument";
    case FoxgloveError::ChannelClosed:
      return "channel closed";
    case FoxgloveError::ServerStartFailed:
      return "server start failed";
    case FoxgloveError::IoError:
      return "I/O error";
    case FoxgloveError::SerializationError:
      return "serialization error";
    case FoxgloveError::ProtocolError:
      return "protocol error";
  }
  return "unknown error";
}

}  // namespace foxglove
