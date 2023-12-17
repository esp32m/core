export const OtaCheckName = 'ota-check';

export enum OtaStateFlags {
  None = 0,
  Updating = 1 << 0,
  Checking = 1 << 1,
  NewVersion = 1 << 2,
}

export type TOtaCheckState = {
  running?: boolean;
  newver?: string;
  error?: unknown;
  lastcheck?: number;
};

export type TOtaState = {
  flags: OtaStateFlags;
  progress?: number;
  total?: number;
};

export enum OtaFeatures {
  None = 0,
  CheckForUpdates = 1 << 0,
  VendorOnly = 1 << 1,
}

export type TOtaConfig = {
  features: OtaFeatures;
  url?: string;
};

export type TOtaCheckConfig = {
  autoupdate?: boolean;
  autocheck?: boolean;
  checkinterval?: number;
};
