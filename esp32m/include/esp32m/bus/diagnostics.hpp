#pragma once

#include <cstdint>

namespace esp32m::modbus {
  enum class Outcome { Success, Timeout, InvalidResponse, OtherError, Unverified };

  struct Transaction {
    bool raw = false;
    uint8_t address = 0;  // Raw protocols are grouped; never infer vendor IDs.
    Outcome outcome = Outcome::OtherError;
    uint64_t durationUs = 0;  // TX/API start to completion; excludes quiet guards.
  };

  // Optional, process-lifetime observer. Called under the UART lock: must not
  // access the bus, publish events, allocate, or block on network/UI operations.
  class Observer {
   public:
    virtual void onTransaction(const Transaction& transaction) = 0;
   protected:
    virtual ~Observer() = default;
  };
}  // namespace esp32m::modbus
