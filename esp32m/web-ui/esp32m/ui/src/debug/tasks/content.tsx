import { FixedSizeList as List, ListChildComponentProps } from 'react-window';
import { Divider } from '@mui/material';

import { ITasksState, Name } from './types';
import { styled } from '@mui/material/styles';
import { CardBox } from '@ts-libs/ui-app';
import { useModuleState } from '../../backend';

const ColNumber = styled('span')({
  display: 'inline-block',
  width: '3em',
  height: '100%',
  textAlign: 'right',
  paddingRight: 10,
});
const ColName = styled('span')({
  height: '100%',
  paddingRight: 5,
  paddingLeft: 5,
  display: 'inline-block',
  width: '16em',
});
const ColTime = styled('span')({
  height: '100%',
  paddingRight: 5,
  paddingLeft: 5,
  display: 'inline-block',
  width: '6em',
});
const ColStack = styled('span')({
  height: '100%',
  paddingRight: 5,
  paddingLeft: 5,
  display: 'inline-block',
  width: '6em',
});

const Row = ({ data, index, style }: ListChildComponentProps) => {
  const item = index < 0 ? ['Id', 'Name', 'CPU', 'Stack'] : data[index];

  return (
    <div style={style}>
      <ColNumber>{item[0]}</ColNumber>
      <ColName>{item[1]}</ColName>
      <ColTime>{index < 0 ? item[2] : `${Math.round(item[2] * 100)}%`}</ColTime>
      <ColStack>{item[3]}</ColStack>
    </div>
  );
};

export const content = () => {
  const state = useModuleState<ITasksState>(Name);
  const { rt, tasks } = state || {};
  if (!tasks) return null;
  const data: Array<[number, string, number, number]> = tasks.map((i) => [
    i[0],
    i[1],
    i[5] / (rt || 1),
    i[6],
  ]);
  data.sort((a, b) => b[2] - a[2]);
  const gp = {
    itemSize: 30,
    itemCount: data.length,
    itemData: data,
    width: '100%',
    height: 300,
  };
  return (
    <CardBox title="Tasks">
      <Row data={null} index={-1} style={{ fontWeight: 'bold' }} />
      <Divider style={{ marginBottom: 7 }} />
      <List {...gp}>{Row}</List>
    </CardBox>
  );
};
