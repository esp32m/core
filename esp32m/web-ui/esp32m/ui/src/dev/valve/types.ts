export enum ValveState {
  Unknown = 'unknown',
  Open = 'open',
  Closed = 'close',
  Invalid = 'invalid',
  Opening = 'opening',
  Closing = 'closing',
}

export type TState = {
  state: ValveState;
  value: number;
  target: number;
  fault?: string | null;
  error?: number;
  errorName?: string;
  stopPending?: boolean;
  stopError?: number;
  stopErrorName?: string;
};

export type TProps = {
  name: string;
  title?: string;
};
