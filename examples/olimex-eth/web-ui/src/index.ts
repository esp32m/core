import { startUi, CaptivePortal, Wifi, System, NetworkInterfaces } from '@esp32m/ui';

startUi({ plugins: [CaptivePortal, System, Wifi, NetworkInterfaces] });
