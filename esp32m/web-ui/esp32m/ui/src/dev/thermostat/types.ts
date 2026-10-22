export type ThermostatConfig = {
  enabled: boolean;
  shadow: boolean;
  targetC: number;
  sensorUid: string;
  strategy: 'automatic' | 'native' | 'power' | 'flow';
  heatHysteresisC: number;
  stopHysteresisC: number;
  proportionalBandC: number;
  highTemperatureC: number;
  minimumFlowC: number;
  maximumFlowC: number;
  minimumRunMs: number;
  minimumOffMs: number;
  lowPowerHoldMs: number;
  sensorFreshnessMs: number;
  feedbackFreshnessMs: number;
  commandTimeoutMs: number;
  allowFullShutdown: boolean;
};

export type TemperatureSource = {
  uid: string;
  name: string;
  device: string;
  enabled: boolean;
  valid: boolean;
  valueC: number | null;
  sampledAtMs: number | null;
};

export type ThermostatState = {
  config: ThermostatConfig;
  climate: {
    mode: 'off' | 'heat';
    action: 'off' | 'idle' | 'heating' | 'fan' | null;
    temperature: number;
    currentTemperature: number | null;
    available: boolean;
  };
  control: {
    ready: boolean;
    active: boolean;
    inhibited: boolean;
    reason: string;
    phase: string;
    strategy: string;
    backend: string;
    fullShutdownSupported: boolean;
    nativeTemperatureSupported: boolean;
    flowSupported: boolean;
    pending: string;
    proposedCommand: string;
    lastCommand: string;
    lastResult: string;
  };
  source: {
    uid: string;
    valid: boolean;
    sampledAtMs: number | null;
  };
  sources: TemperatureSource[];
};

export type ThermostatProps = { name: string; title?: string };
