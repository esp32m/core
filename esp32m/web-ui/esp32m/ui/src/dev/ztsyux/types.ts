export const Name = 'ZTS-YUX';

export type TProps = {
  name?: string;
  title?: string;
  addr?: number;
};

export type TState = [age: number, address: number, value: boolean];

export type TConfig = {
  hst: number;
  het: number;
  delay: number;
  sens: number;
};
