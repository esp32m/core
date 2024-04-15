export const Name = 'lwip-nd6';

export enum NeighState {
  NoEntry = 0,
  Incomplete = 1,
  Reachable = 2,
  Stale = 3,
  Delay = 4,
  Probe = 5,
}

export type TNeigh = [
  ifname: string,
  lladdr: string,
  nexthop: string,
  state: NeighState,
  isrouter: boolean,
  counter: number,
  rit?: number,
  rflags?: number,
];

export type TDest = [dest: string, nexthop: string, pmtu: number, age: number];
export type TPrefix = [prefix: string, ifname: string, it: number];

export type TNd6State = {
  readonly neighs: ReadonlyArray<TNeigh>;
  readonly dests: ReadonlyArray<TDest>;
  readonly prefixes: ReadonlyArray<TPrefix>;
};
