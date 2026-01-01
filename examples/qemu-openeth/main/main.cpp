#include <esp32m/app.hpp>
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
  net::useOta();
  net::useSntp();
  dev::useEsp32();
  net::useOpenethEthernet();
  net::useInterfaces();
  auto &ui = Ui::instance();
  ui.addTransport(ui::Httpd::instance());
  initUi(&ui);
}
