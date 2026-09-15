export const Name = 'MAQI-136';

export type TProps = {
  title?: string;
  addr?: number;
};

export type TState = [
  age: number,
  address: number,
  unit: number,
  decimals: number,
  precision: number,
  min: number,
  max: number,
  value: number,
  offset: number,
  registers: number[],
  calibrated: boolean,
  calibrationMultiplier: number,
  calibrationOffset: number,
];
