import { IProps, IState } from './types';
import {  useModuleState, useSampleAge } from '../..';
import { CardBox } from '@ts-libs/ui-app';
import { NameValueList } from '../../app';

export default ({ name, title }: IProps) => {
  const state = useModuleState<IState>(name);
  const age = useSampleAge(state?.[0], name);
  if (!state || !state.length) return null;
  const [, value] = state;
  if (age < 0 || age > 60 * 1000) return null;
  const list = [];
  if (!isNaN(value)) list.push(['Moisture', `${(value * 100)?.toFixed(1)}%`]);
  return (
    <CardBox title={title || name}>
      <NameValueList list={list} />
    </CardBox>
  );
};
