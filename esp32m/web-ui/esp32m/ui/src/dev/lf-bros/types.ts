export type ControlMode = 'temperature' | 'power';

export type LfBrosState = {
  connected: boolean;
  session: {
    stage: string;
    applicationSeen: boolean;
    sourceSelected: boolean;
    startedUs: number;
    lastApplicationResponseUs: number;
    lastValidRuntimeUs: number;
  };
  command: {
    on: boolean;
    operationMode: 'normal' | 'polr';
    priming: boolean;
    operationModeRestorePending: boolean;
    controlMode: ControlMode;
    temperatureSetpointC: number;
    powerSetpointW: number;
    limits: {
      minimumTemperatureC: number;
      maximumTemperatureC: number;
      minimumPowerW: number;
      maximumPowerW: number;
    };
  };
  heater: {
    valid: boolean;
    stale: boolean;
    partial?: boolean;
    chunkMask?: number;
    telemetrySampledAtMs?: number;
    firmwareTag?: string;
    receivedUs?: number;
    state?: {
      code: number;
      name: string;
      group: string;
      nextCode: number;
      nextName: string;
      nextGroup: string;
      thermalSubstate: number;
      fanControlState: number;
      fanControlNextState: number;
    };
    fault?: {
      active: boolean;
      displayCode: number;
      flags: Record<string, number>;
    };
    physicalDisplay?: {
      operationMode: number;
      operationModeName: string;
      controlMode: number;
      controlModeName: string;
      specialTarget: number;
      temperatureSetpointC?: number;
      powerSetpointW?: number;
      onOffCommand: number;
    };
    pump?: {
      on: boolean;
      mode: 'normal-scheduler' | 'mode1-pumping';
      mode1Pumping: boolean;
      frequencyAvailable: boolean;
      frequencyHz?: number;
      pulseCount: number;
    };
    glow?: {
      on: boolean;
      dutyFraction?: number;
      dutyPercent?: number;
      effectiveResistanceOhm?: number;
      voltageV?: number;
      minimumVoltageV?: number;
      estimatedPowerW?: number;
    };
    fan?: {
      on: boolean;
      speedRpm: number;
      driveFraction?: number;
      targetRpm: number;
    };
    temperature?: {
      sensorA_C?: number;
      sensorB_C?: number;
      chamberC?: number;
      ambientC?: number;
    };
    electrical?: {
      supplyVoltage?: number;
    };
  };
  can: {
    enabled: boolean;
    state: string;
    nominalBitrate: number;
    actualBitrate: number;
    rxFrames: number;
    rxDropped: number;
    hardwareRxDiagnostics: boolean;
    maxHardwareRxPending: number;
    hardwareRxMissEvents: number;
    hardwareDataOverrunEvents: number;
    runtimeRequests: number;
    validSnapshots: number;
    partialSnapshots: number;
    invalidSnapshots: number;
    inferredMissingChunks: number;
    maxInterFrameGapUs: number;
    txAttempts: number;
    txSucceeded: number;
    txFailed: number;
    busErrors: number;
    arbitrationLostErrors: number;
    bitErrors: number;
    formErrors: number;
    stuffErrors: number;
    ackErrors: number;
    errorEvents: number;
    unclassifiedErrorEvents: number;
    errorBursts: number;
    lastErrorEventUs: number;
    lastErrorBurstStartUs: number;
    lastErrorBurstEndUs: number;
    lastErrorBurstEvents: number;
    lastErrorBurstFlags: number;
    lastErrorBurstRequest: number;
    lastErrorBurstOffsetFromRequestUs: number;
    lastErrorHardwareStatus: number;
    lastErrorRxPending: number;
    lastErrorBurstHardwareStatus: number;
    lastErrorBurstMaxRxPending: number;
    txErrorCount: number;
    rxErrorCount: number;
    lastError: number;
  };
};

export type LfBrosProps = {
  name: string;
  title?: string;
};
