import { IProps, IState } from './types';
import { useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

export default ({ name, title }: IProps) => {
  const state = useModuleState<IState>(name);
  if (!state || !state.length) return null;
  const [age = -1, t, value, mv] = state;
  if (age < 0 || age > 60 * 1000) return null;
  const list = [];
  if (!isNaN(t)) list.push(['Temperature', `${t?.toFixed(1)}\u2103`]);
  if (!isNaN(value)) list.push(['ADC reading', `${(value*100).toFixed(2)}%`]);
  if (!isNaN(mv)) list.push(['Measured voltage', `${mv?.toFixed(0)}mV`]);
  return (
    <CardBox title={title || name}>
      <NameValueList list={list as any} />
    </CardBox>
  );
};
