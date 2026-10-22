export const Name = 'HSM-WT10';

export type TProps = {
  name?: string;
  title?: string;
};

export type TState = {
  address: number;
  baud: number;
  valid: boolean;
  sampledAtMs: number;
  alarm: boolean | null;
  analog: number | null;
  communicationPending: boolean;
  lastError: number;
};

export type TSettings = {
  address: number;
  baud: number;
  sensitivity: number;
  alarmDelay: number;
};

export type TConfig = Partial<TSettings> & {
  configReadError?: number;
  pendingAddress?: number;
  pendingBaud?: number;
};

export type TInputs = Record<keyof TSettings, string>;
