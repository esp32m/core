import { IProps, IState } from './types';
import { useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

const Defs: { [key: number]: any } = {
  5386: {
    title: 'Wind speed sensor',
    value: 'Speed',
  },
  5387: {
    title: 'Wind direction sensor',
    value: 'Direction',
  },
};

export default ({ name, title }: IProps) => {
  const state = useModuleState<IState>(name);
  if (!state || !state.length) return null;
  const [age = -1, , model = 0, value] = state;
  if (age < 0 || age > 60 * 1000) return null;
  const list = [];
  if (model) list.push(['Model', model]);
  const def = Defs[model];
  if (def && !isNaN(value)) {
    list.push([def.value, `${value.toFixed(2)}`]);
  }
  return (
    <CardBox title={title || def?.title || name}>
      <NameValueList list={list as any} />
    </CardBox>
  );
};
