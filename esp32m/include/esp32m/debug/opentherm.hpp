#pragma once

#include "esp32m/app.hpp"
#include "esp32m/dev/opentherm.hpp"

#include <map>
#include <mutex>
#include <vector>

namespace esp32m {
  namespace debug {

    /**
     * OpenTherm master debug tool.
     *
     * Acts as an OT master that continuously polls a configurable set of
     * registers from an OT slave (boiler). The current register values are
     * exposed via the AppObject state mechanism and can be viewed live in the
     * UI. The UI also allows writing individual registers to the slave.
     *
     * Usage:
     *   auto master = debug::useMaster(GPIO_NUM_21, GPIO_NUM_22);
     *
     * The instance registers itself with the event system under name
     * "otm-debug". Add the DebugOpenthermMaster plugin to the UI to display it.
     */
    class OpenthermMaster : public virtual opentherm::Master,
                            public virtual AppObject {
     public:
      OpenthermMaster(opentherm::IDriver* driver,
                      const char* name = "otm-debug");
      OpenthermMaster(const OpenthermMaster&) = delete;

     protected:
      bool handleRequest(Request& req) override;
      JsonDocument* getState(RequestContext& ctx) override;
      bool setConfig(RequestContext& ctx) override;
      JsonDocument* getConfig(RequestContext& ctx) override;
      void processResponse(opentherm::Message& msg) override;
      void incomingFrameInvalid() override;

     private:
      class PollModel;

      std::vector<uint8_t> _pollIds;
      std::mutex _snapshotMutex;
      std::map<uint8_t, uint16_t> _snapshot;
      Response* _pendingResponse = nullptr;
      std::mutex _mutexPR;

      static const std::vector<uint8_t>& defaultPollIds();
    };

    /**
     * Creates and registers an OpenthermMaster debug instance.
     * @param rx  GPIO pin connected to OT RX
     * @param tx  GPIO pin connected to OT TX
     */
    OpenthermMaster* useOpenthermMaster(gpio_num_t rx, gpio_num_t tx);

  }  // namespace debug
}  // namespace esp32m
