import { AccessTime as icon } from '@mui/icons-material';
import { Network } from '../shared';
import { Content } from './content';
import { Name } from './types';
import { TContentPlugin } from '@ts-libs/ui-base';
import { Ti18nPlugin } from '@ts-libs/ui-i18n';

export const Sntp: TContentPlugin & Ti18nPlugin = {
  name: Name,
  use: Network,
  content: {
    title: 'SNTP time sync',
    icon,
    component: Content,
    menu: { parent: Network.name },
  },
  i18n: {
    resources: {
      de: {
        'Time zone': 'Zeitzone',
        'Enable time synchronization': 'Zeitsynchronisierung aktivieren',
        'Time zone setting': 'Zeitzoneneinstellung',
        'NTP hostname': 'NTP-Hostname',
        'Interval, s': 'Intervall, s',
        'Last synchronization': 'Letzte Synchronisation',
        'Current on-chip local time': 'Aktuelle Ortszeit auf dem Chip',
        'never happened': 'noch nie',
        'SNTP Time Synchronization': 'SNTP-Zeitsynchronisierung',
        Autodetect: 'Autoerkennung',
        'Synchronize now': 'Jetzt synchronisieren',
        'SNTP time sync': 'SNTP Zeit',
      },
      uk: {
        'Time zone': 'Часовий пояс',
        'Enable time synchronization': 'Увімкнути синхронізацію часу',
        'Time zone setting': 'Вибір часового поясу',
        'NTP hostname': 'NTP сервер',
        'Interval, s': 'Інтервал, с',
        'Last synchronization': 'Остання синхронізація',
        'Current on-chip local time': 'Місцевий час на процесорі',
        'never happened': 'ніколи',
        'SNTP Time Synchronization': 'Синхронізація часу через SNTP',
        Autodetect: 'Автовизначення',
        'Synchronize now': 'Синхронізувати зараз',
        'SNTP time sync': 'Синхронізація часу',
      },
    },
  },
};
