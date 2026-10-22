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
  actualRaw: number;
  targetValid: boolean;
  targetSampledAtMs: number;
  target: number;
  targetRaw: number;
  state: 'unknown' | 'opening' | 'open' | 'closing' | 'closed';
  position: number;
  probeSampledAtMs: number;
  probeValidMask: number;
  probeRegisters: number[];
};

export type TConfig = {
  address: number;
  baud: number;
};
