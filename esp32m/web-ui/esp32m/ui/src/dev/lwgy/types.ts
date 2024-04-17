export const Name = 'LWGY';

export type TProps = {
  name?: string;
  title?: string;
  addr?: number;
};

export type TState = [
  age: number,
  address: number,
  flow: number,
  consumption: number,
  id: number,
  decimals: number,
  density: number,
  flowunit: number,
  range: number,
  mc: number,
];

export type TConfig = {
  um: number;
  ud: number;
  ucf: number;
  ums: number;
  urd: number;
  range: number;
  sse: number;
  dt: number;
  fd: number;
  sc: number;
  mc: number;
};
