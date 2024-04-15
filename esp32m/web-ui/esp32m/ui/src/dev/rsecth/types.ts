export const Name = 'RS-ECTH';

export type TProps = {
  name?: string;
  title?: string;
  addr?: number;
};

export type TState = [
  age: number,
  address: number,
  moisture: number,
  temperature: number,
  conductivity: number,
  salinity: number,
  tds: number,
];

export type TConfig = {
  ctc: number;
  sc: number;
  tdsc: number;
  tcv: number;
  mcc: number;
  ccv: number;
};
