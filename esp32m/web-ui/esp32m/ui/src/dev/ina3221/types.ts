export const Name = 'INA3221';

export interface IProps {
  title?: string;
  addr?: number;
}

export type Channel = [number, number, number];

export interface IState {
  addr: number;
  channels: Array<Channel>;
}
