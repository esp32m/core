import { startUi, CaptivePortal, Wifi, System, NetworkInterfaces, Sntp, use } from "@esp32m/ui";

use(CaptivePortal, System, Wifi, NetworkInterfaces, Sntp);

startUi();
