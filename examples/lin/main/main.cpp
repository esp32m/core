#include <esp32m/app.hpp>
#include <esp32m/debug/sysled.hpp>

#include <esp32m/net/wifi.hpp>
#include <esp32m/net/sntp.hpp>
#include <esp32m/net/ota.hpp>
#include <esp32m/net/mqtt.hpp>

#include <esp32m/dev/esp32.hpp>

#include <esp32m/ui.hpp>
#include <esp32m/ui/httpd.hpp>

#include <dist/ui.hpp>

#include "lin_test.hpp"

// ---------------------------------------------------------------------------
// Node selection
// ---------------------------------------------------------------------------
// Set LIN_NODE_NUM to 1 (master) or 2 (slave) at build time, e.g.:
//   add_compile_definitions(LIN_NODE_NUM=1)
// or pass on the idf.py command line:
//   idf.py build -DEXTRA_CMAKE_ARGS="-DLIN_NODE_NUM=1"
// ---------------------------------------------------------------------------
#ifndef LIN_NODE_NUM
#  error "LIN_NODE_NUM must be defined to 1 (master) or 2 (slave)"
#endif

#if LIN_NODE_NUM == 1
// Node 1 — LIN master
// Seeed XIAO ESP32C6 + TJA1021 transceiver
//   XIAO D5 / GPIO23  →  TJA1021 TXD
//   XIAO D4 / GPIO22  →  TJA1021 RXD
//   TJA1021 LIN pin   →  LIN bus (single wire)
#  define LIN_TX_GPIO  GPIO_NUM_23
#  define LIN_RX_GPIO  GPIO_NUM_22

#elif LIN_NODE_NUM == 2
// Node 2 — LIN slave
// Seeed XIAO ESP32C3 + TJA1021 transceiver
//   XIAO D5 / GPIO7   →  TJA1021 TXD
//   XIAO D4 / GPIO6   →  TJA1021 RXD
//   TJA1021 LIN pin   →  LIN bus (same single wire as master)
#  define LIN_TX_GPIO  GPIO_NUM_7
#  define LIN_RX_GPIO  GPIO_NUM_6

#else
#  error "LIN_NODE_NUM must be 1 or 2"
#endif

// LIN baud rate — must be identical on both nodes.
#define LIN_BAUD  19200U

using namespace esp32m;

extern "C" void app_main() {
  App::Init app;

  net::wifi::addAccessPoint("SSID", "password");
  net::useOta();
  net::useSntp();
  net::useMqtt();
  dev::useEsp32();

  debug::useSysled();

  auto &ui = Ui::instance();
  ui.addTransport(ui::Httpd::instance());
  initUi(&ui);

#if LIN_NODE_NUM == 1
  new LinTestMaster(LIN_TX_GPIO, LIN_RX_GPIO, LIN_BAUD);
#elif LIN_NODE_NUM == 2
  new LinTestSlave(LIN_TX_GPIO, LIN_RX_GPIO, LIN_BAUD);
#endif
}
