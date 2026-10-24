export const Name = 'FlomarvelValve';

export type TProps = {
  title?: string;
  addr?: number;
};

export type TState = {
  address: number;
  actualValid: boolean;
  actualSampledAtMs: number;
  actual: number;
  targetValid: boolean;
  targetSampledAtMs: number;
  target: number;
  state: 'unknown' | 'opening' | 'open' | 'closing' | 'closed';
  position: number;
};

export type TConfig = {
  address: number;
  baud: number;
};
