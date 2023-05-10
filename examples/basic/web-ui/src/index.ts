import { startUi, CaptivePortal, Wifi, System, Mqtt, Sntp, NetworkInterfaces } from '@esp32m/ui';

startUi({ plugins: [CaptivePortal, System, Wifi, Mqtt, Sntp, NetworkInterfaces] });
