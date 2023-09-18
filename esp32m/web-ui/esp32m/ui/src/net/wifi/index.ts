import { Wifi as icon } from '@mui/icons-material';
import { Name } from './types';

import { Content } from './Content';
import { TContentPlugin } from '@ts-libs/ui-base';
import { Network } from '../shared';
import { Ti18nPlugin } from '@ts-libs/ui-i18n';

export * from './ScanList';
export * from './ScannerBox';
export * from './StaInfoBox';
export * from './ApInfoBox';
export * from './CaptivePortal';

export const Wifi: TContentPlugin & Ti18nPlugin = {
  name: Name,
  use: Network,
  content: {
    title: 'WiFi',
    icon,
    component: Content,
    menu: { parent: Network.name },
  },
  i18n: {
    resources: {
      en: {
        CaptivePortalConnected:
          'The device is connected to the WiFi access point {{ssidText}}',
        CaptivePortalDeviceURL:
          'You can now switch to your local WiFi and access this device via http://{{addr}}/',
      },
      de: {
        WiFi: 'W-LAN',
        'WiFi settings': 'WLAN Einstellungen',
        'Saved access points': 'Gespeicherte Zugangspunkte',
        'Available access points': 'Verfügbare Zugangspunkte',
        'WiFi station': 'WLAN-Station',
        Channel: 'Kanal',
        'IP address': 'IP Adresse',
        'IP gateway': 'IP Gateway',
        Clients: 'Teilnehmer',
        'WiFi Access Point': 'WLAN-Zugangspunkt',
        Connect: 'Verbinden',
        'Connect to WiFi Access Point': 'Verbinde zum WLAN-Zugangspunkt',
        'TX power': 'Sendeleistung',
        'Connection succeeded!': 'Verbindung erfolgreich hergestellt!',
        CaptivePortalConnected:
          'Das Gerät ist mit dem WLAN-Zugangspunkt {{ssidText}} verbunden',
        CaptivePortalDeviceURL:
          'Sie können nun zu Ihrem lokalen WLAN wechseln und über http://{{addr}}/ auf dieses Gerät zugreifen',
      },
      uk: {
        'WiFi settings': 'Налаштування WiFi',
        'Saved access points': 'Збережені точки доступу',
        'Available access points': 'Доступні точки доступу',
        'WiFi station': 'WiFi клієнт',
        Channel: 'Канал',
        'IP address': 'IP адреса',
        'IP gateway': 'IP шлюз',
        Clients: 'Клієнти',
        'WiFi Access Point': 'WiFi точка доступу',
        Connect: "З'єднати",
        'Connect to WiFi Access Point': "З'єднати з точкою доступу",
        'TX power': 'Потужність передавача',
        'Connection succeeded!': "З'єднання успішно встановлено!",
        CaptivePortalConnected:
          'Пристрій підсключено до точки доступу WiFi {{ssidText}}',
        CaptivePortalDeviceURL:
          'Тепер ви можете переключитись на ваш звичайний WiFi і керувати пристроєм, відкривши в браузері http://{{addr}}/',
      },
      it: {
        'WiFi settings': 'Impostazioni WiFi',
        'Saved access points': 'Punti di accesso salvati',
        'Available access points': 'Punti di accesso disponibili',
        'WiFi station': 'Stazione WiFi',
        Channel: 'Canale',
        'IP address': 'Indirizzo IP',
        'IP gateway': 'Gateway IP',
        Clients: 'Client',
        'WiFi Access Point': 'Punto di accesso WiFi',
        Connect: 'Connetti',
        'Connect to WiFi Access Point': 'Connetti al punto di accesso WiFi',
        'TX power': 'Potenza TX',
        'Connection succeeded!': 'Connessione riuscita!',
        CaptivePortalConnected:
          'Il dispositivo è connesso al punto di accesso WiFi {{ssidText}}',
        CaptivePortalDeviceURL:
          'Ora puoi passare al tuo WiFi locale e accedere a questo dispositivo tramite http://{{addr}}/',
      },
    },
  },
};
