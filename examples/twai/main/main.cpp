#include <esp32m/app.hpp>
#include <esp32m/debug/sysled.hpp>

#include <esp32m/net/wifi.hpp>
#include <esp32m/net/sntp.hpp>
#include <esp32m/net/ota.hpp>
#include <esp32m/net/mqtt.hpp>
#include <esp32m/integrations/ha/mqtt.hpp>
#include <esp32m/integrations/influx/mqtt.hpp>

#include <esp32m/dev/esp32.hpp>

#include <esp32m/ui.hpp>
#include <esp32m/ui/httpd.hpp>

#include <dist/ui.hpp>

#include "can_test.hpp"

// ---------------------------------------------------------------------------
// Node selection
// ---------------------------------------------------------------------------
// Set CAN_NODE_NUM to 1 or 2 at build time, e.g. by adding to CMakeLists.txt:
//   target_compile_definitions(${COMPONENT_LIB} PRIVATE CAN_NODE_NUM=1)
// ---------------------------------------------------------------------------
#ifndef CAN_NODE_NUM
#  error "CAN_NODE_NUM must be defined to 1 or 2"
#endif

#if CAN_NODE_NUM == 1
// Node 1: Seeed XIAO ESP32C6 + Adafruit CAN Pal (TJA1051T/3)
//   XIAO D5 / GPIO23  ->  TXD
//   XIAO D4 / GPIO22  ->  RXD
//   Module powered from 3.3 V; termination switch ON.
#  define CAN_TX_GPIO  GPIO_NUM_23
#  define CAN_RX_GPIO  GPIO_NUM_22
#  define CAN_TX_ID    0x001U

#elif CAN_NODE_NUM == 2
// Node 2: Seeed XIAO ESP32C3 + M5Stack Mini CAN Unit U179 (TJA1051T/3)
//   XIAO D5 / GPIO7   ->  Yellow / CAN_TX
//   XIAO D4 / GPIO6   ->  White  / CAN_RX
//   Module powered from 5 V / VBUS.
#  define CAN_TX_GPIO  GPIO_NUM_7
#  define CAN_RX_GPIO  GPIO_NUM_6
#  define CAN_TX_ID    0x002U

#else
#  error "CAN_NODE_NUM must be 1 or 2"
#endif

// Classic CAN bitrate – must be identical on both nodes.
// Start with 250 kbit/s; increase to 500k or 1M once the bus is verified.
#define CAN_BITRATE  250000U

using namespace esp32m;

extern "C" void app_main() {
  App::Init app;

  net::wifi::addAccessPoint("MOON", "eterna1ife");
  net::useOta();
  net::useSntp();
  net::useMqtt();
  //  net::mqtt::StatePublisher::instance();
  //  integrations::ha::useMqtt();
  //  integrations::influx::useMqtt();
  dev::useEsp32();

  debug::useSysled();

  auto &ui = Ui::instance();
  ui.addTransport(ui::Httpd::instance());
  initUi(&ui);

  new CanTestNode(CAN_TX_GPIO, CAN_RX_GPIO, CAN_TX_ID, CAN_BITRATE);
}
