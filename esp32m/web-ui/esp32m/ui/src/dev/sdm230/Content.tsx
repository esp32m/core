import { CardBox } from '@ts-libs/ui-app';
import { NameValueList } from '../../app';
import { useModuleState } from '../../backend';
import { Name, IState, IProps } from './types';

export default ({ title, addr }: IProps) => {
  const state = useModuleState<IState>(Name);
  if (!state) return null;
  const [age = -1, a, te = 0, ee = 0, ie = 0, v, i, ap = 0, rap = 0, pf, f] =
    state;
  if (age < 0 || age > 60 * 1000) return null;
  if (addr && addr != a) return null;
  const list = [];
  if (ap || rap)
    list.push([
      'Power (active/reactive)',
      `${ap.toFixed(2)} / ${rap.toFixed(2)} kW`,
    ]);
  if (pf) list.push(['Power factor', `${pf.toFixed(2)}`]);
  if (te || ee || ie)
    list.push(['Energy (imp/exp)', `${ie.toFixed(2)} / ${ee.toFixed(2)} kW*h`]);
  if (v) list.push(['Voltage', `${v.toFixed(2)} V`]);
  if (i) list.push(['Current', `${i.toFixed(2)} A`]);
  if (f) list.push(['Frequency', `${f.toFixed(2)} Hz`]);
  if (!list.length) return null;
  if (!addr && a) list.push(['MODBUS address', a]);
  return (
    <CardBox title={title || Name}>
      <NameValueList list={list as any} />
    </CardBox>
  );
};
