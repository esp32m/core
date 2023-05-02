import { TIpv4Info } from '..';

export const Name = 'wifi';

export const enum WifiMode {
  Null,
  Sta,
  Ap,
  ApSta,
  Max,
}
export const enum WifiFlags {
  None,
  Initialized = 1 << 0,
  StaRunning = 1 << 1,
  StaConnected = 1 << 2,
  StaGotIp = 1 << 3,
  ApRunning = 1 << 4,
  Scanning = 1 << 5,
  ScanDone = 1 << 6,
}
export const enum WifiPower {
  dBm_20 = 80, // 20dBm
  dBm_18 = 72, // 18dBm
  dBm_16 = 66, // 16dBm
  dBm_15 = 60, // 15dBm
  dBm_14 = 56, // 14dBm
  dBm_13 = 52, // 13dBm
  dBm_11 = 44, // 11dBm
  dBm_8 = 34, // 8dBm
  dBm_7 = 28, // 7dBm
  dBm_5 = 20, // 5dBm
  dBm_2 = 8, // 2dBm
}

export const enum WifiAuth {
  None,
  WEP,
  WPA_PSK,
  WPA2_PSK,
  WPA_WPA2_PSK,
  WPA2_ENTERPRISE,
}

export type TApInfo = {
  ssid: string;
  mac: string;
  ip: TIpv4Info;
  cli: number;
};

export type TStaInfo = {
  ssid: string;
  bssid: string;
  mac: string;
  ip: TIpv4Info;
  rssi: number;
};

export type TScanEntry = [
  ssid: string,
  auth: WifiAuth,
  rssi: number,
  channel: number,
  bssid: string
];

export type ScanEntries = Array<TScanEntry>;

export type TWifiState = {
  mode: WifiMode;
  ch: number;
  ch2: number;
  flags: WifiFlags;
  ap?: TApInfo;
  sta?: TStaInfo;
};

export const enum ApEntryFlags {
  None = 0,
  Fallback = 1,
}

export type TApEntry = [
  id: number, // id
  ssid: string, // ssid
  pwd: string, // pwd - masked,
  flags: ApEntryFlags, // flags
  failcount: number, // failcount
  bssid: string // bssid
];

export type ApEntries = Array<TApEntry>;

export type TWifiConfig = {
  txp: number;
  aps?: ApEntries;
};
