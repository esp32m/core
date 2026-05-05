export const Name = 'PMWS';

export type TProps = {
  name?: string;
  title?: string;
  addr?: number;
};

export type TState = [
  age: number,
  address: number,
  pm25: number,
  pm10: number,
  pm1: number,
  temperature?: number,
  humidity?: number,
];

export type TConfig = {
  pm25c: number;
  pm10c: number;
  pm1c: number;
  tc?: number;
  hc?: number;
};
