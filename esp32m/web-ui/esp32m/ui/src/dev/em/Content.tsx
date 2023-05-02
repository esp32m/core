import { IState, IProps } from './types';
import {  useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

export default ({ name, title }: IProps) => {
  const state = useModuleState<IState>(name);
  if (!state) return null;
  const [age = -1, data = {}] = state;
  if (age < 0 || age > 60 * 1000) return null;
  const { voltage, current, frequency } = data;
  const ap = data['power-apparent'];
  const rap = data['power-reactive'];
  const pf = data['power-factor'];
  const ee = data['energy-active-exp'];
  const ie = data['energy-active-imp'];
  const list = [];
  if (ap || rap)
    list.push([
      'Power (active/reactive)',
      `${ap.toFixed(2)} / ${rap.toFixed(2)} kW`,
    ]);
  if (pf) list.push(['Power factor', `${pf.toFixed(2)}`]);

  if (ee || ie)
    list.push(['Energy (imp/exp)', `${ie.toFixed(2)} / ${ee.toFixed(2)} kW*h`]);
  if (voltage) list.push(['Voltage', `${voltage.toFixed(2)} V`]);
  if (current) list.push(['Current', `${current.toFixed(2)} A`]);
  if (frequency) list.push(['Frequency', `${frequency.toFixed(2)} Hz`]);
  if (!list.length) return null;
  return (
    <CardBox title={title || name}>
      <NameValueList list={list} />
    </CardBox>
  );
};
