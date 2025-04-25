import { FixedSizeList as List, ListChildComponentProps } from 'react-window';
import { CardBox } from '@ts-libs/ui-app';
import { useModuleState } from '../../../backend';
import { Name, NeighState, TDest, TNd6State, TNeigh, TPrefix } from './types';

const NeighRow = ({
  data,
  index,
  style,
}: ListChildComponentProps<ReadonlyArray<TNeigh>>) => {
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
    itemSize: 30,
    itemCount: neighs.length,
    itemData: neighs,
    width: '100%',
    height: 200,
  };
  return (
    <CardBox title="Neighbors">
      <List {...gp}>{NeighRow}</List>
    </CardBox>
  );
};

const DestRow = ({
  data,
  index,
  style,
}: ListChildComponentProps<ReadonlyArray<TDest>>) => {
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
    itemSize: 30,
    itemCount: dests.length,
    itemData: dests,
    width: '100%',
    height: 200,
  };
  return (
    <CardBox title="Destinations">
      <List {...gp}>{DestRow}</List>
    </CardBox>
  );
};

const PrefixRow = ({
  data,
  index,
  style,
}: ListChildComponentProps<ReadonlyArray<TPrefix>>) => {
  const [prefix, ifname, it] = data[index];

  return <div style={style}>{`${ifname} ${prefix} ${it}`}</div>;
};

const Prefixes = ({ prefixes }: { prefixes: ReadonlyArray<TPrefix> }) => {
  if (!prefixes?.length) return null;
  const gp = {
    itemSize: 30,
    itemCount: prefixes.length,
    itemData: prefixes,
    width: '100%',
    height: 200,
  };
  return (
    <CardBox title="Prefixes">
      <List {...gp}>{PrefixRow}</List>
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
