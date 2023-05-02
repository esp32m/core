export enum ValveState {
  Unknown = 'unknown',
  Open = 'open',
  Closed = 'close',
  Invalid = 'invalid',
  Opening = 'opening',
  Closing = 'closing',
}

export interface IState {
  state: ValveState;
  value: number;
  target: number;
}

export interface IProps {
  name: string;
  title?: string;
}
