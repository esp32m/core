#include "esp32m/logging.hpp"

#include <algorithm>
#include <new>

namespace esp32m::log {
  LogMessage* LogMessage::copyTo(void* destination, size_t capacity,
                                 bool* truncated) const {
    const size_t overhead = sizeof(LogMessage) + _tasklen + _namelen;
    if (!destination || capacity <= overhead) return nullptr;
    auto length = std::min(messagelen(), capacity - overhead);
    // Avoid introducing an incomplete UTF-8 sequence when truncating a message.
    if (length < messagelen()) {
      size_t bytes = length - 1;
      while (bytes && (static_cast<unsigned char>(message()[bytes]) & 0xc0) == 0x80)
        --bytes;
      length = bytes + 1;
    }
    if (truncated) *truncated = length < messagelen();
    return new (destination) LogMessage(overhead + length, level(), stamp(),
        task(), _tasklen, name(), _namelen, message(), length);
  }
}  // namespace esp32m::log
