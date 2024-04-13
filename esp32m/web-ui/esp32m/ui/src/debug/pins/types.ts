import { TPlugin } from '@ts-libs/plugins';
import { ComponentType } from 'react';

export const enum FeatureType {
  Digital,
  ADC,
  DAC,
  PWM,
  Pcnt,
  LEDC,
}

export enum PinFlagBits {
  Input = 0,
  Output,
  OpenDrain,
  PullUp,
  PullDown,
  ADC,
  DAC,
  PulseCounter,
  LEDC,
  PAD,
}

export const enum PinFlags {
  None = 0,
  Input = 1 << PinFlagBits.Input,
  Output = 1 << PinFlagBits.Output,
  OpenDrain = 1 << PinFlagBits.OpenDrain,
  PullUp = 1 << PinFlagBits.PullUp,
  PullDown = 1 << PinFlagBits.PullDown,
  ADC = 1 << PinFlagBits.ADC,
  DAC = 1 << PinFlagBits.DAC,
  PulseCounter = 1 << PinFlagBits.PulseCounter,
  LEDC = 1 << PinFlagBits.LEDC,
  PAD = 1 << PinFlagBits.PAD,
}

export const enum FeatureStatus {
  NotSupported,
  Supported,
  Enabled,
}

export type TPinState<T = any> = {
  features: Array<FeatureStatus>;
  flags: PinFlags;
  state: T;
};

export const enum PinMode {
  Default = 0,
  Input = 1 << 0,
  Output = 1 << 1,
  OpenDrain = 1 << 2,
  PullUp = 1 << 3,
  PullDown = 1 << 4,
}

export type TDigitalState = { high: boolean; mode: PinMode };

export type TPWMState = { enabled: boolean; freq: number; duty: number };

export const enum AdcAtten {
  Db0 = 0,
  Db2_5 = 1,
  Db6 = 2,
  Db11 = 3,
}

export type TADCState = {
  atten: AdcAtten;
  width: number;
  value: number;
  mv: number;
};

export type TDACState = {
  value: number;
};

export enum PcntLevelAction {
  Keep,
  Inverse,
  Hold,
}

export enum PcntEdgeAction {
  Hold,
  Increase,
  Decrease,
}

export type TPcntState = {
  enabled: boolean;
  value: number;
  freq: number;
  pea: PcntEdgeAction;
  nea: PcntEdgeAction;
  hla: PcntLevelAction;
  lla: PcntLevelAction;
  gns: number;
};

export type TFeatureInfo = {
  feature: FeatureType;
  name: string;
  component: ComponentType;
  validationSchema?: any;
  beforeSubmit?: (values: any) => any;
};

export type TDebugPinFeaturePlugin = TPlugin & {
  debug: {
    pin: {
      features: Array<TFeatureInfo>;
    };
  };
};
