import { startUi, useCaptivePortal, useWifi, useSystem, useMqtt, useBme280, useDsts, useRelay } from "@esp32m/ui";

useCaptivePortal();
useSystem();
useWifi();
useMqtt();

// useRelay([["fan", "Fan"]]);
// useBme280();
// useDsts();

startUi();
