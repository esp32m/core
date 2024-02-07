export const Name = 'dsts';

export interface IProbe extends Array<string> {
  0: string; // code
  1: string; // label
}

export interface IProps {
  title?: string;
  probes?: Array<IProbe>;
}

export type TProbeState = [
  code: string, // code
  resolution: number, // resolution
  parasite: boolean, // parasite
  temperature: number, // temperature
  failed: number,
  label: string,
];

export type States = Array<TProbeState>;
