export const Name = 'dsts';

export interface IProbe extends Array<string> {
  0: string; // code
  1: string; // label
}

export interface IProps {
  title?: string;
  probes?: Array<IProbe>;
}

export interface IState extends Array<string | number | boolean> {
  0: string; // code
  1: number; // resolution
  2: boolean; // parasite
  3: number; // temperature
}

export type States = Array<IState>;
