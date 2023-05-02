export const Name = 'gpio';

export interface IGpioState {
  pins: { [key: number]: string };
}

export const enum PinMode {
  Undefined,
  Digital,
  Adc,
  Dac,
  CosineWave,
  Ledc,
  SigmaDelta,
  PulseCounter,
  Touch,
}

export const enum GpioMode {
  Disabled = 0,
  Input = 1,
  Output = 2,
  InputOutput = 3,
  OutputOd = 2 + 4,
  InputOutputOd = 3 + 4,
}

export const enum PullMode {
  Up = 0,
  Down = 1,
  Both = 2,
  Floating = 3,
}

export const enum AdcAtten {
  Db0 = 0,
  Db2_5 = 1,
  Db6 = 2,
  Db11 = 3,
}

export const enum CwScale {
  A1 = 0,
  A1_2 = 1,
  A1_4 = 2,
  A1_8 = 3,
}

export const enum CwPhase {
  P0 = 2,
  P180 = 3,
}

export const enum LedcIntr {
  Disable = 0,
  FadeEnd = 1,
}

export const enum LedcMode {
  HighSpeed = 0,
  LowSpeed = 1,
}

export const enum LedcClkCfg {
  Auto,
  RefTick,
  Apb,
  Rtc8m,
}

export const enum PcntCountMode {
  Disable,
  Increment,
  Decrement,
}

export const enum TouchTieOpt {
  Low,
  High,
}

export type PinConfig = Array<number>;
export type LedctConfig = Array<number>;
export type LedctsConfig = Array<LedctConfig>;

export interface IGpioConfig {
  pins: { [key: number]: PinConfig };
  ledcts: LedctsConfig;
}
