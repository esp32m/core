export const Name = 'SM10';

export type TProps = {
  name?: string;
  title?: string;
  addr?: number;
};

export type TRtcSync =
  | 'unknown'
  | 'invalid'
  | 'valid'
  | 'waiting-for-time'
  | 'sync-failed'
  | 'synced'
  | 'read-failed';

export type TState = {
  sampledAtMs: number;
  address: number;
  baud: number;
  /** Cubic metres per hour. */
  flow?: number | null;
  /** Fine cumulative reading in cubic metres. */
  total?: number | null;
  /** Meter calendar in ESP32 local time, without a timezone suffix. */
  rtc: string | null;
  rtcValid: boolean;
  rtcSampledAtMs: number;
  rtcSync: TRtcSync;
  status?: number;
  temperature?: number | null;
  voltage?: number | null;
  bootCount?: number;
  lastError?: string;
};

export type TConfig = {
  address: number;
  baud: number;
};

export type THistoryBank = 'hourly' | 'daily' | 'monthly' | 'yearly';

export type THistory = {
  bank: THistoryBank;
  /** Oldest to newest cumulative snapshots in cubic metres; no timestamps. */
  values: number[];
  rtc: string | null;
  current: number;
};
