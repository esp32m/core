export const Name = 'FlomarvelValve';

export type TProps = {
  title?: string;
  addr?: number;
};

export type TState = {
  address: number;
  actualValid: boolean;
  actualAge: number;
  actual: number;
  actualRaw: number;
  targetValid: boolean;
  targetAge: number;
  target: number;
  targetRaw: number;
  probeAge: number;
  probeValidMask: number;
  probeRegisters: number[];
};

export type TConfig = {
  address: number;
  baud: number;
};
