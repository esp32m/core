import { Memory } from '@mui/icons-material';
export { SystemSummary } from './system-summary';
import { Content } from './content';
import { TContentPlugin } from '@ts-libs/ui-base';
import { Ti18nPlugin } from '@ts-libs/ui-i18n';

export const System: TContentPlugin & Ti18nPlugin = {
  name: 'system',
  content: { title: 'System', icon: Memory, component: Content },
  i18n: {
    resources: {
      de: {
        System: 'System',
        Administration: 'Verwaltung',
        'Do you really want to restart the CPU ?':
          'Wollen Sie die CPU wirklich neu starten?',
        'Do you really want to reset settings to their defaults ?':
          'Möchten Sie die Einstellungen wirklich auf die Standardwerte zurücksetzen?',
        'Do you really want to perform firmware update ?':
          'Wollen Sie wirklich ein Firmware-Update durchführen?',
        Update: 'Aktualisieren',
        Reset: 'Zurücksetzen',
        'system restart': 'Neustart',
        'reset settings': 'Einstellungen zurücksetzen',
        'This will reset hostname to application default, proceed?':
          'Dadurch wird der Hostname auf den Anwendungsstandard zurückgesetzt, fortfahren?',
        'This option will enable sending debug logs to the remote rsyslog server over UDP, proceed?':
          'Diese Option ermöglicht das Senden von Debug-Protokollen an den entfernten rsyslog-Server über UDP, fortfahren?',
        'Host name': 'Hostname',
        'Host name or IP address of rsyslog server':
          'Hostname oder IP-Adresse des rsyslog-Servers',
        'Application settings': 'Anwendungseinstellungen',
        'Application name': 'Anwendungsname',
        'CPU Local time': 'CPU Ortszeit',
        Uptime: 'Betriebszeit',
        'Firmware version / build time': 'Firmware-Version / Build-Zeit',
        'UI version / build time': 'UI-Version / Build-Zeit',
        'SDK version': 'SDK-Version',
        'Firmware size': 'Firmware-Größe',
        'Application summary': 'Zusammenfassung der Bewerbung',
        'CPU / features': 'CPU / Funktionen',
        unknown: 'Unbekannt',
        cores: 'Kerne',
        'CPU temperature': 'CPU-Temperatur',
        'CPU Frequency': 'CPU-Frequenz',
        'Reset reason': 'Neustartgrund',
        'Flash size / speed / mode': 'Blitzgröße / geschwindigkeit / modus',
        'RAM size / free (% fragmented)': 'RAM Größe / frei (% fragmentiert)',
        'SPIFFS size / free (% used)': 'SPIFFS Größe / frei (% fragmentiert)',
        'Hardware summary': 'Hardware-Zusammenfassung',
        'Min. CPU frequency': 'Min. CPU-Frequenz',
        'Max. CPU frequency': 'Max. CPU-Frequenz',
        'Power management': 'Energieverwaltung',
        'System settings': 'Systemeinstellungen',
        'System summary': 'Systemzusammenfassung',
        'Enable remote logging': 'Remoteprotokollierung aktivieren',
        'Firmware update is in progress': 'Firmware-Update wird durchgeführt',
      },
      uk: {
        System: 'Система',
        Administration: 'Управління',
        'Do you really want to restart the CPU ?':
          'Ви дійсно бажаєте пезавантажити процесор?',
        'Do you really want to reset settings to their defaults ?':
          'Ви дійсно бажаєте скинути всі налаштування?',
        'Do you really want to perform firmware update ?':
          'Ви дійсно бажаєте оновити мікропрограму?',
        'Firmware URL': 'URL мікропрограми',
        Update: 'Оновити',
        Reset: 'Скинути',
        'system restart': 'перезавантажити',
        'reset settings': 'скинути налаштування',
        'This will reset hostname to application default, proceed?':
          'Це призведе до скидання імені хоста до значення за замовчуванням, продовжити?',
        'This option will enable sending debug logs to the remote rsyslog server over UDP, proceed?':
          'Цей параметр дозволить надсилати журнали налагодження на віддалений сервер rsyslog через UDP. Продовжити?',
        'Host name': "Ім'я хоста",
        'Host name or IP address of rsyslog server':
          "Ім'я хоста або IP-адреса сервера rsyslog",
        'Application settings': 'Налаштування додатку',
        'Application name': 'Найменування додатку',
        'CPU Local time': 'Місцевий час процесора',
        Uptime: 'Час роботи процесора',
        'Firmware version / build time': 'Версія прошивки / час збірки',
        'UI version / build time': 'Версія UI / час збірки',
        'SDK version': 'Версія SDK',
        'Firmware size': 'Розмір прошивки',
        'Application summary': 'Інформація про мікропрограму',
        'CPU / features': 'Процесор / функції',
        unknown: 'невідомо',
        cores: 'ядра',
        'CPU temperature': 'Температура процесора',
        'CPU Frequency': 'Частота процесора',
        'Reset reason': 'Причина перезавантаження',
        'Flash size / speed / mode': 'Розмір / швидкість / режим флеш',
        'RAM size / free (% fragmented)': 'Розмір / вільно (% фрагм.) ОЗП',
        'SPIFFS size / free (% used)': 'Розмір / вільно (% використано) SPIFFS',
        'Hardware summary': 'Огляд обладнання',
        'Min. CPU frequency': 'Мін. частота CPU',
        'Max. CPU frequency': 'Макс. частота CPU',
        'Power management': 'Управління споживанням',
        'System settings': 'Налаштування системи',
        'System summary': 'Огляд системи',
        'Enable remote logging': 'Увімкнути віддалене ведення журналу',
        'Firmware update is in progress': 'Триває оновлення прошивки',
      },
    },
  },
};
