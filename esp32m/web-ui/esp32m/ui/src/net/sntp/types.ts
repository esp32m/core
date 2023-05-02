export const Name = 'sntp';

export interface IConfig {
  enabled: boolean;
  host: string;
  tz: number;
  dst: number;
}

export const enum Status {
  Reset,
  Completed,
  InProgress,
}

export interface IState {
  status: Status;
  synced: number;
  time: string;
}
