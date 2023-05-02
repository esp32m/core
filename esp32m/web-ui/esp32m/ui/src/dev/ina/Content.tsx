import { IState, IProps } from './types';
import { useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

export default ({ name, title }:IProps) => {
  const state=useModuleState<IState>(name);
  if (!state) return null;
  if (!state || !state.length) return null;
  const [age = -1, , t, voltage, shuntVoltage, current, power] = state;
  if (age < 0 || age > 60 * 1000) return null;
  const list = [];
  if (t) list.push(['Chip type', t]);
  if (!isNaN(voltage)) list.push(['Voltage', `${voltage?.toFixed(2)} V`]);
  if (!isNaN(current)) list.push(['Current', `${current?.toFixed(5)} A`]);
  if (!isNaN(power)) list.push(['Power', `${power?.toFixed(5)} W`]);
  if (!isNaN(shuntVoltage)) list.push(['Shunt voltage', `${shuntVoltage?.toFixed(6)} V`]);
  if (!list.length) return null;

  return (
    <CardBox title={title || name}>
      <NameValueList list={list} />
    </CardBox>
  );
};
