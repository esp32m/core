import { RssFeed } from '@mui/icons-material';
import { Network } from '../shared';
import { Content } from './Content';
import { TContentPlugin } from '@ts-libs/ui-base';
import { Ti18nPlugin } from '@ts-libs/ui-i18n';

export { MqttStateBox } from './State';

export const Mqtt: TContentPlugin & Ti18nPlugin = {
  name: 'mqtt',
  use: Network,
  content: {
    title: 'MQTT client',
    icon: RssFeed,
    component: Content,
    menu: { parent: Network.name },
  },
  i18n: {
    resources: {
      de: {
        'MQTT client': 'MQTT Client',
        'MQTT client state': 'MQTT Client Status',
        'Enable MQTT client': 'MQTT Client aktivieren',
        'MQTT client settings': 'MQTT Client Einstellungen',
        'Keep alive period, s': 'Keep-Alive-Periode, s',
        'Network timeout, s': 'Netzwerk-Timeout, s',
        'SSL certificate URL': 'URL des SSL-Zertifikats',
        'Clear local certificate cache':
          'Löschen Sie den lokalen Zertifikatcache',
        'Connection state': 'Verbindungszustand',
        'Published messages': 'Veröffentlichte Nachrichten',
        'Received messages': 'Nachricht erhalten',
        'This will remove cached SSL certificate and reload it from the specified URL on the next connection, proceed?':
          'Dadurch wird das zwischengespeicherte SSL-Zertifikat entfernt und bei der nächsten Verbindung von der angegebenen URL neu geladen, fortfahren?',
        connected: 'verbunden',
        'not connected': 'nicht verbunden',
      },
      uk: {
        'Client name': 'Ідентифікатор клієнта',
        'MQTT client': 'MQTT клієнт',
        'MQTT client state': 'Стан MQTT клієнта',
        'Enable MQTT client': 'Увімкнути MQTT клієнта',
        'MQTT client settings': 'Налаштування MQTT клієнта',
        'Keep alive period, s': 'Тайм-аут підтримки активності, с',
        'Network timeout, s': 'Тайм-аут мережі, с',
        'SSL certificate URL': 'URL SSL сертифіката',
        'Clear local certificate cache': 'Очистити кеш SSL сертифіката',
        'Connection state': "Стан з'єднання",
        'Published messages': 'Опубліковано повідомлень',
        'Received messages': 'Отримано повідомлень',
        'This will remove cached SSL certificate and reload it from the specified URL on the next connection, proceed?':
          "Ця дія видалить збережений SSL сертифікат із внутрішнього сховища, сертифікат буде перезавантажено під час наступного з'єднання, продовжити?",
        connected: 'з`єднаний',
        'not connected': "немає з'єднання",
      },
    },
  },
};
