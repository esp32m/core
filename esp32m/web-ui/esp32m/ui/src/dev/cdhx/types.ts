export type CdhxMode = 'temperature' | 'power' | 'unknown';

export type CdhxService = {
  minimumPumpHz?: number;
  maximumPumpHz?: number;
  minimumFanRpm?: number;
  maximumFanRpm?: number;
  configuredVoltageV?: number;
  hallCount?: number;
  glowPowerLevel?: number;
};

export type CdhxOperatingUpdate =
  | {
      mode: 'temperature';
      temperatureSetpointC: number;
      fixedPowerSettingRaw?: number;
    }
  | {
      mode: 'power';
      fixedPowerSettingRaw: number;
      temperatureSetpointC?: number;
    };

export type CdhxConfigUpdate =
  | { service: Required<CdhxService>; operating?: never }
  | { operating: CdhxOperatingUpdate; service?: never };

export type CdhxConfiguration = {
  bootId: string;
  revision: number;
  authority: boolean;
  active: boolean;
  mode: Exclude<CdhxMode, 'unknown'> | null;
  value: number | null;
  temperatureSetpointC: number | null;
  fixedPowerSettingRaw: number | null;
  canApply: boolean;
  modeChangeAvailable: boolean;
  ambientAvailable: boolean;
  powerAvailable: boolean;
  phase: string;
  reason: string | null;
  stockMode: CdhxMode;
  submittedMode: CdhxMode;
  submittedValue: number | null;
  service: CdhxService;
  stockService?: CdhxService;
  submittedService?: CdhxService;
  serviceAuthority: boolean;
  serviceCanApply: boolean;
  serviceReason: string | null;
  serviceWritable: boolean;
};

export type CdhxSerialFaultDetail = {
  side: 'display' | 'heater';
  kind:
    | 'uart-frame-error'
    | 'uart-fifo-overflow'
    | 'uart-buffer-full'
    | 'uart-break'
    | 'uart-parity-error'
    | 'echo-mismatch'
    | 'echo-late'
    | 'echo-missing';
  atUs: number;
  expectedByte: number | null;
  receivedByte: number | null;
  echoPosition: number;
  echoPending: number;
  echoDeadlineUs: number;
  bufferedRx: number;
  queuedEvents: number;
  rxLevel: number;
  workerGapUs: number;
  stepAgeUs?: number;
  rxReadAtUs?: number;
  rxReadBytes?: number;
};

export type CdhxState = {
  connected: boolean;
  bridgeActive: boolean;
  fault: string | null;
  model: string;
  configuration?: CdhxConfiguration;
  bypass: {
    supported: boolean;
    active: boolean;
    requested: boolean;
    qualifiedPairs: number;
  };
  display: {
    valid: boolean;
    sampledAtMs: number;
    mode: CdhxMode;
    ambientC?: number;
    requestedRaw?: number;
    temperatureSetpointC?: number;
    minimumTemperatureC: number;
    maximumTemperatureC: number;
    requestHex?: string;
  };
  control: {
    available: boolean;
    temperatureOverrideC: number | null;
    fixedPowerOverrideRaw: number | null;
    fixedPowerMinimumRaw: number | null;
    fixedPowerMaximumRaw: number | null;
    pendingCommand: string | null;
    lastCommand: string;
    lastCommandResult: string;
    emittedRequestHex?: string;
    emittedRequestedRaw?: number;
  };
  heater: {
    valid: boolean;
    sampledAtMs: number;
    state?: number;
    stateName?: string;
    status?: number;
    statusName?: string;
    supplyVoltageV?: number;
    fanRpm?: number;
    fanVoltageV?: number;
    bodyTemperatureC?: number;
    glowVoltageV?: number;
    glowCurrentA?: number;
    pumpSettingHz?: number;
    retainedConfigByte?: number;
    identityFailures?: number;
    settingFeedbackRaw?: number;
    constant20?: number;
    reserved21?: number;
    responseHex?: string;
  };
  serial: {
    rxDisplay: number;
    rxHeater: number;
    forwardedRequests: number;
    forwardedResponses: number;
    crcErrors: number;
    framingErrors: number;
    echoErrors: number;
    timeouts: number;
    lastError: number;
    faultDetail?: CdhxSerialFaultDetail | null;
    workerMaxGapUs?: number;
    workerMaxStepUs?: number;
    rtosTickUs?: number;
    echoDelayedReads?: number;
    echoMaxServiceDelayUs?: number;
  };
};

export type CdhxProps = {
  name: string;
  title?: string;
};

export type CdhxAction =
  | { on: boolean }
  | { temperatureSetpointC: number }
  | { fixedPowerSettingRaw: number }
  | { resetBridge: true }
  | { bypass: boolean };
