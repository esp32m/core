import { Name, IState, IProps } from './types';
import {  useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

export default ({ title, addr }: IProps) => {
  const state = useModuleState<IState>(Name);
  if (!state) return null;
  const [age = -1, a, pv = 0, ad = 0] = state;
  if (age < 0 || age > 60 * 1000) return null;
  if (addr && addr != a) return null;
  const list = [];
  if (pv) list.push(['PV', `${pv}`]);
  if (ad) list.push(['AD', `${ad}`]);
  if (!list.length) return null;
  if (!addr && a) list.push(['MODBUS address', a]);
  return (
    <CardBox title={title || Name}>
      <NameValueList list={list as any} />
    </CardBox>
  );
};
