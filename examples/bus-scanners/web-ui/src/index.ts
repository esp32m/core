import { startUi, CaptivePortal, Wifi, System, Mqtt, Sntp, NetworkInterfaces, I2CScanner, OwbScanner, ModbusScanner } from '@esp32m/ui';

startUi({ plugins: [CaptivePortal, System, Wifi, Mqtt, Sntp, NetworkInterfaces, I2CScanner, OwbScanner, ModbusScanner] });
