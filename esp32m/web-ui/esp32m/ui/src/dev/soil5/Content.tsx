import { Name, TState, TProps } from './types';
import { useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';

export const Content = ({
  name = Name,
  title = 'Soil sensor',
  addr,
}: TProps) => {
  const state = useModuleState<TState>(name);
  if (!state) return null;
  const [
    age = -1,
    a,
    moisture,
    temperature,
    conductivity,
    ph,
    nitrogen,
    phosphorus,
    potassium,
    salinity,
    tds,
  ] = state;
  if (age < 0 || age > 60 * 1000) return null;
  if (addr && addr != a) return null;
  const list = [];
  list.push(['Moisture', `${moisture?.toFixed(1)}`]);
  list.push(['Temperature', `${temperature?.toFixed(1)}`]);
  list.push(['Conductivity', `${conductivity}`]);
  list.push(['PH', `${ph}`]);
  list.push(['Nitrogen', `${nitrogen}`]);
  list.push(['Phosphorus', `${phosphorus}`]);
  list.push(['Potassium', `${potassium}`]);
  list.push(['Salinity', `${salinity}`]);
  list.push(['TDS', `${tds}`]);
  if (!list.length) return null;
  if (!addr && a) list.push(['MODBUS address', a]);
  return (
    <CardBox title={title || name}>
      <NameValueList list={list} />
    </CardBox>
  );
};
