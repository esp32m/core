import { IProps, IState } from './types';
import { useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

export default ({ name, title }: IProps) => {
  const state = useModuleState<IState>(name);
  if (!state || !state.length) return null;
  const [age = -1, t, h] = state;
  if (age < 0 || age > 60 * 1000) return null;
  const list = [];
  if (!isNaN(t)) list.push(["Temperature", `${t?.toFixed(2)} \u2103`]);
  if (!isNaN(h)) list.push(["Humidity", `${h?.toFixed(0)} %`]);
  return (
    <CardBox title={title || name}>
      <NameValueList list={list} />
    </CardBox>
  );
};
