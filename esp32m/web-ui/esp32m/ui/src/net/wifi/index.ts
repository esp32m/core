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
        'Connect to WiFi Access Point': 'Verbinden mit dem WLAN-Zugangspunkt',
        'TX power': 'Sendeleistung',
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
      },
    },
  },
};
