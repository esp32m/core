export const Name = 'sntp';

export type TConfig = {
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

export type TState = {
  status: Status;
  synced: number;
  time: string;
}
