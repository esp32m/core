import { startUi, CaptivePortal, Wifi, System, Mqtt, use } from "@esp32m/ui";

use(CaptivePortal, System, Wifi, NetworkInterfaces, Mqtt);

// useRelay([["fan", "Fan"]]);
// useBme280();
// useDsts();

startUi();
