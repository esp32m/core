export const Name = 'BME280';

export type TProps = {
  title?: string;
  addr?: number;
}

export type TState = {
  addr: number;
  temperature: number;
  pressure: number;
  humidity: number;
}
