export const Name = 'ESP32';

export type THeapState = {
  size: number;
  free: number;
  min: number;
  max: number;
}

export enum ChipModel {
  ESP32 = 1,
  ESP32S2 = 2,
  ESP32C3 = 5,
  ESP32S3 = 9,
  ESP32C2 = 12,
  ESP32C6 = 13,
  ESP32H2 = 16,
  ESP32P4 = 18,
  ESP32C61 = 20,
  ESP32C5 = 23,
  ESP32H21 = 25,
  ESP32H4 = 28,
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
  Unknown = 0,
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
  USB,
  JTAG,
  EFuse,
  PowerGlitch,
  CPULockup,
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

export const Features: { [key: number]: string } = {
  [ChipFeatures.EmbFlash]: 'EMB_FLASH',
  [ChipFeatures.WifiBgn]: '802.11bgn',
  [ChipFeatures.Ble]: 'BLE',
  [ChipFeatures.Bt]: 'BT',
  [ChipFeatures.Ieee802154]: 'IEEE 802.15.4',
  [ChipFeatures.EmbPsram]: 'EMB_PSRAM',
};
