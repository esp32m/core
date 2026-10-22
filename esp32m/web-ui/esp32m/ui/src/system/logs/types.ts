export const Name = 'logs';

export type LogInfo = {
  schema: number;
  supportsQuotaPercent?: boolean;
  available: boolean;
  filesystem: string;
  label: string;
  totalBytes: number;
  freeBytes: number;
  reserveBytes: number;
  // Filesystem free space with managed log files reclaimed (legacy: absent).
  quotaBaseBytes?: number;
  minQuotaBytes: number;
  maxQuotaBytes: number;
  pageBytes: number;
  maxPageBytes: number;
};

export type LogState = {
  available: boolean;
  enabled: boolean;
  ready: boolean;
  usedBytes: number;
  quotaBytes: number;
  quotaPercent?: number;
  maxAgeDays?: number;
  effectiveQuotaBytes: number;
  freeBytes: number;
  reserveBytes: number;
  quotaBaseBytes?: number;
  fileCount: number;
  dropped: number;
  truncated: number;
  pendingBytes: number;
  timeValid: boolean;
  lastError: string;
};

export type LogConfig = {
  enabled: boolean;
  quotaBytes: number;
  // 0 or absent: fixed bytes. 1..100: authoritative percentage of quotaBaseBytes.
  quotaPercent?: number;
  maxAgeDays: number;
};

export type LogFile = {
  id: string;
  size: number;
  active: boolean;
  bootId: string;
  // Unix milliseconds; null means the segment's date is unknown.
  firstTime: number | null;
  lastTime: number | null;
};

export type LogList = {
  generation: string;
  files: LogFile[];
  nextOffset?: number;
};

export type LogPage = {
  id: string;
  generation: string;
  offset: number;
  end: number;
  size: number;
  text: string;
  eof: boolean;
};
