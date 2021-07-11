#include <esp32m/app.hpp>
#include <esp32m/net/ota.hpp>
#include <esp32m/net/wifi.hpp>
// #include <esp32m/net/mqtt.hpp>

#include <esp32m/dev/esp32.hpp>
// #include <esp32m/dev/bme280.hpp>
// #include <esp32m/dev/dsts.hpp>
// #include <esp32m/dev/relay.hpp>

#include <esp32m/log/console.hpp>
#include <esp32m/log/udp.hpp>

#include <esp32m/ui.hpp>
#include <esp32m/ui/httpd.hpp>

#include <ui.hpp>

// All ESP32 manager code is in under esp32m namespace
using namespace esp32m;

extern "C" void app_main() {
  // Initialize ESP32 manager. This must be the first line in any esp32m
  // application
  App::Init app;
  // Send log messages to UART0
  log::addAppender(&log::Console::instance());
  // Send log messages to rsyslog daemon over UDP.
  // It will look for the syslog.lan name on the local network.
  // If you want to specify a different name or IP address - just pass it to the
  // Udp() constructor
  log::addBufferedAppender(new log::Udp());
  // Queue messages instead of sending them right away. Consumes slightly more
  // RAM, but doesn't block current thread, the messages are being sent
  // concurrently.
  log::useQueue();
  // Redirect ESP-IDF messages to our logger so we can send them over to rsyslog
  // and/or other medium.
  log::hookEsp32Logger();
  // Add support for Over The Air firmware updates.
  net::useOta();
  // Replace SSID/password with your WiFi credentials
  net::wifi::addAccessPoint("SSID", "password");
  // Retrieve various pieces of info about ESP32 module to be displayed in the
  // UI.
  dev::useEsp32();
  // Start embedded HTTP server for UI. Just type http://IP_addres_of_your_ESP32
  // in your web browser to open the UI
  initUi(new Ui(new ui::Httpd()));
  // Add more devices/modules here:
  // dev::useBme280();
  // dev::useBuzzer(GPIO_NUM_15);
  // dev::useRelay("fan", gpio::pin(GPIO_NUM_32));
  // net::useMqtt();
}
