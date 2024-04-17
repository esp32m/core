export const Name = 'PTMB';

export type TProps = {
  name?: string;
  title?: string;
  addr?: number;
};

export type TState = [age: number, address: number, value: number];

export type TConfig = {
  unit: number;
  decimals: number;
  zr: number;
  fp: number;
};
