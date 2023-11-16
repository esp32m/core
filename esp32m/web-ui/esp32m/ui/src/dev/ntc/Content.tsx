import { IProps, IState } from './types';
import {useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

export default ({ name, title }: IProps) => {
  const state=useModuleState<IState>(name);
  if (!state || !state.length) return null;
  const [age = -1, value] = state;
  if (age < 0 || age > 60 * 1000) return null;
  const list = [];
  if (!isNaN(value)) list.push(['Temperature', `${value?.toFixed(2)}\u2103`]);
  return (
    <CardBox title={title || name}>
      <NameValueList list={list as any} />
    </CardBox>
  );
};
