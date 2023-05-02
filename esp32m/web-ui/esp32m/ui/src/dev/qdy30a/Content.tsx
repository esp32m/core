import { Name, IState, IProps } from './types';
import {  useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

const un = ['?', 'cm', 'mm', 'MPa', 'Pa', 'kPa', 'MA'];

export default ({ title, addr }: IProps) => {
  const state = useModuleState<IState>(Name);
  if (!state) return null;
  const [age = -1, a, units = 0, decimals = 0, , , value = 0, regs] = state;
  if (age < 0 || age > 60 * 1000) return null;
  if (addr && addr != a) return null;
  const list = [];
  if (value) list.push(['Level', `${value.toFixed(decimals)} ${un[units]}`]);
  if (regs && regs[10]) list.push(['Undocumented', `${regs[10]}`]);
  if (!list.length) return null;
  if (!addr && a) list.push(['MODBUS address', a]);
  return (
    <CardBox title={title || Name}>
      <NameValueList list={list} />
    </CardBox>
  );
};
