import { startUi, CaptivePortal, Wifi, System, Mqtt, use } from "@esp32m/ui";

use(CaptivePortal, System, Wifi, Mqtt);

startUi();
