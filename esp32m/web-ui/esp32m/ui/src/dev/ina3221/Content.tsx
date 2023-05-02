import { Name, IState, IProps } from './types';
import { useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

export default ({ title, addr }: IProps) => {
  const state = useModuleState<IState>(Name);
  if (!state) return null;
  const { addr: a, channels } = state;
  if (addr && addr != a) return null;
  const list: Array<any> = [];
  channels.forEach((c, i) => {
    if (c && c.length)
      list.push([
        'Channel ' + i,
        `${c[0]?.toFixed(2)} V / ${c[1]?.toFixed(2)} mA`,
      ]);
  });
  if (!list.length) return null;

  return (
    <CardBox title={title || 'INA3221'}>
      <NameValueList list={list as any} />
    </CardBox>
  );
};
