export const Name = 'lwip-mld6';

export type TGroup = [
  addr: string,
  state: number,
  last: boolean,
  timer: number,
  use: number,
];

export type TMld6State = {
  [ifname: string]: ReadonlyArray<TGroup>;
};
