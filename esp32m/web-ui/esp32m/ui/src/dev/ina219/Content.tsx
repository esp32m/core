import { Name, IState, IProps } from './types';
import { useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

export default ({ title, addr }: IProps) => {
  const state=useModuleState<IState>(Name);
  if (!state) return null;
  const { addr: a, current, voltage, shuntVoltage, power } = state;
  if (addr && addr != a) return null;
  const list: Array<any> = [];
  list.push(['Voltage', `${voltage} V`]);
  list.push(['Current', `${current} mA`]);
  list.push(['Power', `${power} W`]);
  list.push(['Shunt voltage', `${shuntVoltage} mV`]);
  if (!list.length) return null;

  return (
    <CardBox title={title || 'INA219'}>
      <NameValueList list={list as any} />
    </CardBox>
  );
};
