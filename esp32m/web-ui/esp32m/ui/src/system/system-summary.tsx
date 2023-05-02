import { CardBox } from '@ts-libs/ui-app';
import { useModuleState } from '../backend';
import { formatBytes } from '../utils';
import { Name, IHardwareState, IAppState, ResetReasons } from './types';
import { NameValueList } from '../app';
import { useTimeTranslation } from '@ts-libs/ui-i18n';

export const SystemSummary = () => {
  const hw = useModuleState<IHardwareState>(Name);
  const app = useModuleState<IAppState>('app');
  const { elapsed } = useTimeTranslation();
  const list = [];
  if (app) {
    const { time, uptime } = app || {};
    if (time)
      list.push(['CPU Local time', new Date(time * 1000).toUTCString()]);
    if (uptime) list.push(['Uptime', elapsed(uptime)]);
  }
  if (hw) {
    const { heap, chip, spiffs } = hw || {};
    if (chip) {
      if (chip.revision || chip.freq)
        list.push(['CPU frequency', `${chip.freq} MHz`]);
      if (chip.temperature)
        list.push([
          'CPU temperature',
          Math.round(chip.temperature) + ' \u2103',
        ]);
      if (chip.rr) list.push(['Reset reason', ResetReasons[chip.rr]]);
    }
    if (heap && heap.size && heap.free) {
      list.push([
        'RAM size / free (% fragmented)',
        formatBytes(heap.size) +
          ' / ' +
          formatBytes(heap.free) +
          ' (' +
          Math.round(((heap.free - heap.max) * 100) / heap.free) +
          '%)',
      ]);
    }
    if (spiffs && spiffs.size && spiffs.free) {
      list.push([
        'SPIFFS size / free (% used)',
        formatBytes(spiffs.size) +
          ' / ' +
          formatBytes(spiffs.free) +
          ' (' +
          Math.round(((spiffs.size - spiffs.free) * 100) / spiffs.size) +
          '%)',
      ]);
    }
  }
  if (!list.length) return null;

  return (
    <CardBox title="System summary">
      <NameValueList list={list} />
    </CardBox>
  );
};
