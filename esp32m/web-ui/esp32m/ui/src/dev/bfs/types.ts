export const Name = 'bfs';

export type TProps = {
  name?: string;
  title?: string;
  addr?: number;
};

export type TChannel = {
  raw?: number;
  value?: number;
  sampledAtMs: number;
  valid: boolean;
};

export type TState = {
  address: number;
  baud: number;
  pressureSetpoint: TChannel;
  pressure: TChannel;
  frequencySetpoint: TChannel;
  frequency: TChannel;
  voltage: TChannel;
  current: TChannel;
  temperature: TChannel;
  status: TChannel;
};

export type TConfig = { address: number; baud: number };

export type TRegister = {
  address: number;
  raw?: number;
  error?: string | number;
  skipped?: boolean;
  cached?: boolean;
};

export type TParameterDescriptor = {
  address: number;
  id: string;
  name: string;
  group: string;
  unit?: string;
  scale?: number;
  min?: number;
  max?: number;
  step?: number;
  options?: Array<{ value: number; label: string }>;
  writable: boolean;
  reason?: string;
  inferred?: boolean;
  signed?: boolean;
};

export type TParameter = TRegister & TParameterDescriptor;
export type TDialogRead = {
  aborted?: boolean;
  abortReason?: 'disabled' | 'changed' | 'failures' | 'deadline';
};
export type TParameters = TDialogRead & {
  parameters: TRegister[];
  cached?: boolean;
};
export type TFaults = TDialogRead & { values: TRegister[] };
export type TWriteResult = {
  accepted: boolean;
  verified: boolean;
  raw?: number;
  writeError?: string | number;
  readError?: string | number;
};
