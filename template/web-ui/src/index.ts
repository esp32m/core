import { startUi, useCaptivePortal, useWifi, useSystem, useMqtt } from "@esp32m/ui";

useCaptivePortal();
useSystem();
useWifi();
useMqtt();

// useRelay([["fan", "Fan"]]);
// useBme280();
// useDsts();

startUi();
