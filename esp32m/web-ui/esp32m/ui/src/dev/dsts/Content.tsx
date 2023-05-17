import { Name, States, IProps, IProbe } from './types';
import { useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

export default ({ probes, title }: IProps) => {
  const state = useModuleState<States>(Name);
  if (!state || !state.length) return null;
  const list = [];
  let label;
  for (const entry of state) {
    if (probes) {
      let found: IProbe | undefined = undefined;
      for (const p of probes)
        if (p[0] == entry[0]) {
          found = p;
          label = p[1];
          break;
        }
      if (!found) continue;
    }
    let t: number | string = entry[3];
    if (typeof t == 'number') t = t.toFixed(2) + ' \u2103';
    else t = '?';
    const f = Math.round((entry[4] as number) * 100);
    if (f < 100) t += `(${100 - f}% failures)`;
    list.push([label || entry[5] || entry[0], t]);
  }
  if (!list.length) return null;

  return (
    <CardBox
      title={
        title || 'Dallas temperature sensor' + (list.length > 1 ? 's' : '')
      }
    >
      <NameValueList list={list} />
    </CardBox>
  );
};
