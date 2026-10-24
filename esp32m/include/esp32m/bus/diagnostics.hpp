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

  // Optional, process-lifetime observer. Preparation runs before the UART lock
  // and may reserve bounded diagnostic storage. It must not access the bus or
  // prevent a transaction when storage is unavailable. Completion runs under
  // the UART lock: no bus access, publication, allocation or network/UI waits.
  class Observer {
   public:
    virtual void prepareTransaction(bool, uint8_t) {}
    virtual void onTransaction(const Transaction& transaction) = 0;
   protected:
    virtual ~Observer() = default;
  };
}  // namespace esp32m::modbus
