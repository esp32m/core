export const Name = 'ESP32';

export type THeapState = {
  size: number;
  free: number;
  min: number;
  max: number;
}

export const enum ChipModel {
  ESP32 = 1,
  ESP32S2 = 2,
  ESP32C3 = 5,
  ESP32H4 = 6,
  ESP32S3 = 9,
  ESP32C2 = 12,
  ESP32C6 = 13,
  ESP32H2 = 16,
  POSIX = 999,
}

export const enum ChipFeatures {
  EmbFlash = 1 << 0,
  WifiBgn = 1 << 1,
  Ble = 1 << 4,
  Bt = 1 << 5,
  Ieee802154 = 1 << 6,
  EmbPsram = 1 << 7,
}

export enum ResetReason {
  Unknown = 1,
  PowerOn,
  ExtPin,
  Software,
  Panic,
  IntWdt,
  TaskWdt,
  OtherWdt,
  DeepSleep,
  Brownout,
  SDIO,
}

export type TChipState = {
  model: ChipModel;
  revision: number;
  cores: number;
  features: ChipFeatures;
  freq: number;
  efreq: number;
  mac: number;
  temperature: number;
  rr: ResetReason;
}

export type TFlashState = {
  size: number;
  speed: number;
  mode: number;
}

export type TSpiffsState = {
  size: number;
  free: number;
}

export type TPsramState = {
  size: number;
  free: number;
  min: number;
  max: number;
}

export type THardwareState = {
  heap: THeapState;
  chip: TChipState;
  flash: TFlashState;
  spiffs: TSpiffsState;
  psram: TPsramState;
}

export type TAppState = {
  name: string;
  time: number;
  uptime: number;
  built: string;
  version: string;
  sdk: string;
  size: number;
}

export interface ISystemConfig {
  pm: [number, number, boolean];
}

export const Models = {
  [ChipModel.ESP32]: 'ESP32',
  [ChipModel.ESP32S2]: 'ESP32S2',
  [ChipModel.ESP32C3]: 'ESP32C3',
  [ChipModel.ESP32H4]: 'ESP32H4',
  [ChipModel.ESP32S3]: 'ESP32S3',
  [ChipModel.ESP32C2]: 'ESP32C2',
  [ChipModel.ESP32C6]: 'ESP32C6',
  [ChipModel.ESP32H2]: 'ESP32H2',
  [ChipModel.POSIX]: 'POSIX',
};
export const Features: { [key: number]: string } = {
  [ChipFeatures.EmbFlash]: 'EMB_FLASH',
  [ChipFeatures.WifiBgn]: '802.11bgn',
  [ChipFeatures.Ble]: 'BLE',
  [ChipFeatures.Bt]: 'BT',
  [ChipFeatures.Ieee802154]: 'IEEE 802.15.4',
  [ChipFeatures.EmbPsram]: 'EMB_PSRAM',
};
