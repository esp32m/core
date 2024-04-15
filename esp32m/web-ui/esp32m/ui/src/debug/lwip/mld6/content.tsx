import { CardBox } from '@ts-libs/ui-app';
import { useModuleState } from '../../../backend';
import { Name, TMld6State } from './types';

export const content = () => {
  const state = useModuleState<TMld6State>(Name);
  if (!state) return null;
  return <CardBox title="MLD6 groups">{JSON.stringify(state)};</CardBox>;
};
