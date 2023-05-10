#include <esp32m/app.hpp>
#include <esp32m/log/udp.hpp>
#include <esp32m/log/console.hpp>
#include <esp32m/net/wifi.hpp>
#include <esp32m/net/ota.hpp>
#include <esp32m/net/ethernet.hpp>
#include <esp32m/net/interfaces.hpp>
#include <esp32m/net/sntp.hpp>

#include <esp32m/dev/esp32.hpp>

#include <esp32m/ui.hpp>
#include <esp32m/ui/httpd.hpp>

#include <dist/ui.hpp>

using namespace esp32m;

extern "C" void app_main()
{
  App::Init app;
  log::addAppender(&log::Console::instance());
  log::addBufferedAppender(new log::Udp());
  log::useQueue();
  log::hookEsp32Logger();
  log::hookUartLogger();
  net::useOta();
  net::useSntp();
  net::useOlimexEthernet();
  dev::useEsp32();
  net::useInterfaces();
  initUi(new Ui(new ui::Httpd()));
}
