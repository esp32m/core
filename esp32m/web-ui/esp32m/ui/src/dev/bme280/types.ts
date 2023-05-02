export const Name = 'BME280';

export interface IProps {
  title?: string;
  addr?: number;
}

export interface IState {
  addr: number;
  temperature: number;
  pressure: number;
  humidity: number;
}
