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
}

export type TProps = {
  name: string;
  title?: string;
}
