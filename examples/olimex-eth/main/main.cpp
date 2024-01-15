#include <esp32m/app.hpp>
#include <esp32m/net/wifi.hpp>
#include <esp32m/net/ota.hpp>
#include <esp32m/net/ethernet.hpp>
#include <esp32m/net/interfaces.hpp>
#include <esp32m/net/sntp.hpp>

#include <esp32m/dev/esp32.hpp>

#include <esp32m/ui.hpp>
#include <esp32m/ui/httpd.hpp>

#if CONFIG_ESP32M_UI
  #include <dist/ui.hpp>
#endif

using namespace esp32m;

extern "C" void app_main()
{
  App::Init app;
  net::useOta();
  net::useSntp();
  dev::useEsp32();
  net::useWifi();
  net::useOlimexEthernet();
  net::useInterfaces();
  
# if CONFIG_ESP32M_WS_API
# if CONFIG_ESP32M_UI
  // Start embedded HTTP server for UI. 
  initUi(new Ui(new ui::Httpd()));
# else
  // when using localhost to serve UI, only start ws server on device
  // and run  'yarn start' in ../build/webui to serve web app
  new Ui(new ui::Httpd());
# endif
# endif

}
