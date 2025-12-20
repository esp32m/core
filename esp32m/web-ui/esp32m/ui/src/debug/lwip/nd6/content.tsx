import { List, RowComponentProps } from 'react-window';
import { CardBox } from '@ts-libs/ui-app';
import { useModuleState } from '../../../backend';
import { Name, NeighState, TDest, TNd6State, TNeigh, TPrefix } from './types';
import { data } from 'react-router';

const NeighRow = ({
  data,
  index,
  style,
}: RowComponentProps<{ data: ReadonlyArray<TNeigh> }>) => {
  const [ifname, lladdr, nexthop, state/*, isrouter, counter, rit, rflags*/] =
    data[index];

  return (
    <div
      style={style}
    >{`${ifname} ${lladdr} ${nexthop} ${NeighState[state] || `state=${state}`}`}</div>
  );
};

const Neighs = ({ neighs }: { neighs: ReadonlyArray<TNeigh> }) => {
  if (!neighs?.length) return null;
  const gp = {
    rowHeight: 30,
    rowCount: neighs.length,
    rowProps: { data: neighs },
    width: '100%',
    height: 200,
    rowComponent: NeighRow,
  };
  return (
    <CardBox title="Neighbors">
      <List {...gp} />
    </CardBox>
  );
};

const DestRow = ({
  data,
  index,
  style,
}: RowComponentProps<{ data: ReadonlyArray<TDest> }>) => {
  const [dest, nexthop, pmtu, age] = data[index];

  return (
    <div
      style={style}
    >{`${dest} ${pmtu} ${age} ${nexthop == dest ? '' : nexthop}`}</div>
  );
};

const Dests = ({ dests }: { dests: ReadonlyArray<TDest> }) => {
  if (!dests?.length) return null;
  const gp = {
    rowHeight: 30,
    rowCount: dests.length,
    rowProps: { data: dests },
    width: '100%',
    height: 200,
    rowComponent: DestRow,
  };
  return (
    <CardBox title="Destinations">
      <List {...gp} />
    </CardBox>
  );
};

const PrefixRow = ({
  data,
  index,
  style,
}: RowComponentProps<{ data: ReadonlyArray<TPrefix> }>) => {
  const [prefix, ifname, it] = data[index];

  return <div style={style}>{`${ifname} ${prefix} ${it}`}</div>;
};

const Prefixes = ({ prefixes }: { prefixes: ReadonlyArray<TPrefix> }) => {
  if (!prefixes?.length) return null;
  const gp = {
    rowHeight: 30,
    rowCount: prefixes.length,
    rowProps: { data: prefixes },
    width: '100%',
    height: 200,
    rowComponent: PrefixRow,
  };
  return (
    <CardBox title="Prefixes">
      <List {...gp} />
    </CardBox>
  );
};


export const content = () => {
  const state = useModuleState<TNd6State>(Name);
  if (!state) return null;
  return (
    <>
      <Neighs neighs={state.neighs}></Neighs>
      <Dests dests={state.dests}></Dests>
      <Prefixes prefixes={state.prefixes}></Prefixes>
    </>
  );
};
