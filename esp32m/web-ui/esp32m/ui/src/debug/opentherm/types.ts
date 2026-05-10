export const Name = 'otm-debug';

/** How to interpret/display a raw uint16 register value. */
export const enum RegFormat {
  U16 = 'u16',    // unsigned 16-bit integer
  S16 = 's16',    // signed 16-bit integer
  F88 = 'f88',    // fixed-point 8.8 (value / 256)
  U8Pair = 'u8p', // two unsigned 8-bit values  (high / low)
  S8Pair = 's8p', // two signed 8-bit values    (high / low)
  FlagPair = 'fp', // two flag bytes             (0xHH / 0xLL)
}

export type TRegDef = {
  name: string;
  format: RegFormat;
  writable?: boolean;
};

/** Known OpenTherm Data IDs with their names and data formats. */
export const RegDefs: Record<number, TRegDef> = {
  0:   { name: 'Status',                       format: RegFormat.FlagPair },
  1:   { name: 'TSet',                          format: RegFormat.F88,  writable: true },
  2:   { name: 'MConfigMMemberIDcode',          format: RegFormat.FlagPair },
  3:   { name: 'SConfigSMemberIDcode',          format: RegFormat.FlagPair },
  4:   { name: 'Command',                       format: RegFormat.U8Pair },
  5:   { name: 'ASFflags',                      format: RegFormat.FlagPair },
  6:   { name: 'RBPflags',                      format: RegFormat.FlagPair },
  7:   { name: 'CoolingControl',               format: RegFormat.F88,  writable: true },
  8:   { name: 'TSetCH2',                       format: RegFormat.F88,  writable: true },
  9:   { name: 'TrOverride',                    format: RegFormat.F88 },
  10:  { name: 'TSP',                           format: RegFormat.U8Pair },
  11:  { name: 'TSPindexTSPvalue',              format: RegFormat.U8Pair },
  12:  { name: 'FHBsize',                       format: RegFormat.U8Pair },
  13:  { name: 'FHBindexFHBvalue',              format: RegFormat.U8Pair },
  14:  { name: 'MaxRelModLevelSetting',         format: RegFormat.F88,  writable: true },
  15:  { name: 'MaxCapacityMinModLevel',        format: RegFormat.U8Pair },
  16:  { name: 'TrSet',                         format: RegFormat.F88,  writable: true },
  17:  { name: 'RelModLevel',                   format: RegFormat.F88 },
  18:  { name: 'CHPressure',                    format: RegFormat.F88 },
  19:  { name: 'DHWFlowRate',                   format: RegFormat.F88 },
  20:  { name: 'DayTime',                       format: RegFormat.U8Pair },
  21:  { name: 'Date',                          format: RegFormat.U8Pair },
  22:  { name: 'Year',                          format: RegFormat.U16 },
  23:  { name: 'TrSetCH2',                      format: RegFormat.F88,  writable: true },
  24:  { name: 'Tr',                            format: RegFormat.F88,  writable: true },
  25:  { name: 'Tboiler',                       format: RegFormat.F88 },
  26:  { name: 'Tdhw',                          format: RegFormat.F88 },
  27:  { name: 'Toutside',                      format: RegFormat.F88 },
  28:  { name: 'Tret',                          format: RegFormat.F88 },
  29:  { name: 'Tstorage',                      format: RegFormat.F88 },
  30:  { name: 'Tcollector',                    format: RegFormat.F88 },
  31:  { name: 'TflowCH2',                      format: RegFormat.F88 },
  32:  { name: 'Tdhw2',                         format: RegFormat.F88 },
  33:  { name: 'Texhaust',                      format: RegFormat.S16 },
  34:  { name: 'Theatexchanger',                format: RegFormat.F88 },
  35:  { name: 'FanSpeed',                      format: RegFormat.U16 },
  36:  { name: 'ElectricalCurrentBurnerFlame',  format: RegFormat.F88 },
  37:  { name: 'TRoomCH2',                      format: RegFormat.F88 },
  38:  { name: 'RelativeHumidity',              format: RegFormat.U8Pair },
  48:  { name: 'TdhwSetUBTdhwSetLB',            format: RegFormat.S8Pair },
  49:  { name: 'MaxTSetUBMaxTSetLB',            format: RegFormat.S8Pair },
  50:  { name: 'HcratioUBHcratioLB',           format: RegFormat.S8Pair },
  51:  { name: 'Remoteparameter4boundaries',    format: RegFormat.S8Pair },
  52:  { name: 'Remoteparameter5boundaries',    format: RegFormat.S8Pair },
  53:  { name: 'Remoteparameter6boundaries',    format: RegFormat.S8Pair },
  54:  { name: 'Remoteparameter7boundaries',    format: RegFormat.S8Pair },
  55:  { name: 'Remoteparameter8boundaries',    format: RegFormat.S8Pair },
  56:  { name: 'TdhwSet',                       format: RegFormat.F88,  writable: true },
  57:  { name: 'MaxTSet',                       format: RegFormat.F88,  writable: true },
  58:  { name: 'Hcratio',                       format: RegFormat.F88,  writable: true },
  59:  { name: 'Remoteparameter4',              format: RegFormat.F88,  writable: true },
  60:  { name: 'Remoteparameter5',              format: RegFormat.F88,  writable: true },
  61:  { name: 'Remoteparameter6',              format: RegFormat.F88,  writable: true },
  62:  { name: 'Remoteparameter7',              format: RegFormat.F88,  writable: true },
  63:  { name: 'Remoteparameter8',              format: RegFormat.F88,  writable: true },
  70:  { name: 'StatusVH',                      format: RegFormat.FlagPair },
  71:  { name: 'ControlSetpointVH',             format: RegFormat.U16 },
  72:  { name: 'ASFFaultCodeVH',               format: RegFormat.FlagPair },
  73:  { name: 'DiagnosticCodeVH',              format: RegFormat.U16 },
  74:  { name: 'ConfigMemberIDVH',              format: RegFormat.FlagPair },
  75:  { name: 'OpenthermVersionVH',            format: RegFormat.F88 },
  76:  { name: 'VersionTypeVH',                 format: RegFormat.U8Pair },
  77:  { name: 'RelativeVentilation',           format: RegFormat.U16 },
  78:  { name: 'RelativeHumidityExhaustAir',    format: RegFormat.U8Pair },
  79:  { name: 'CO2LevelExhaustAir',            format: RegFormat.U16 },
  80:  { name: 'SupplyInletTemperature',        format: RegFormat.F88 },
  81:  { name: 'SupplyOutletTemperature',       format: RegFormat.F88 },
  82:  { name: 'ExhaustInletTemperature',       format: RegFormat.F88 },
  83:  { name: 'ExhaustOutletTemperature',      format: RegFormat.F88 },
  84:  { name: 'ActualExhaustFanSpeed',         format: RegFormat.U16 },
  85:  { name: 'ActualSupplyFanSpeed',          format: RegFormat.U16 },
  86:  { name: 'RemoteParameterSettingVH',      format: RegFormat.FlagPair },
  87:  { name: 'NominalVentilationValue',       format: RegFormat.U16 },
  88:  { name: 'TSPNumberVH',                   format: RegFormat.U8Pair },
  89:  { name: 'TSPEntryVH',                    format: RegFormat.U8Pair },
  90:  { name: 'FaultBufferSizeVH',             format: RegFormat.U8Pair },
  91:  { name: 'FaultBufferEntryVH',            format: RegFormat.U8Pair },
  98:  { name: 'RFstrengthbatterylevel',        format: RegFormat.U8Pair },
  99:  { name: 'OperatingMode_HC1_HC2_DHW',     format: RegFormat.U8Pair },
  100: { name: 'RemoteOverrideFunction',        format: RegFormat.FlagPair },
  101: { name: 'SolarStorageMaster',            format: RegFormat.FlagPair },
  102: { name: 'SolarStorageASFflags',         format: RegFormat.FlagPair },
  109: { name: 'ElectricityProducerStarts',     format: RegFormat.U16 },
  110: { name: 'ElectricityProducerHours',      format: RegFormat.U16 },
  111: { name: 'ElectricityProduction',         format: RegFormat.U16 },
  112: { name: 'CumulativElectricityProduction',format: RegFormat.U16 },
  113: { name: 'BurnerUnsuccessfulStarts',      format: RegFormat.U16 },
  114: { name: 'FlameSignalTooLow',             format: RegFormat.U16 },
  115: { name: 'OEMDiagnosticCode',             format: RegFormat.U16 },
  116: { name: 'BurnerStarts',                  format: RegFormat.U16 },
  117: { name: 'CHPumpStarts',                  format: RegFormat.U16 },
  118: { name: 'DHWPumpValveStarts',            format: RegFormat.U16 },
  119: { name: 'DHWBurnerStarts',               format: RegFormat.U16 },
  120: { name: 'BurnerOperationHours',          format: RegFormat.U16 },
  121: { name: 'CHPumpOperationHours',          format: RegFormat.U16 },
  122: { name: 'DHWPumpValveOperationHours',    format: RegFormat.U16 },
  123: { name: 'DHWBurnerOperationHours',       format: RegFormat.U16 },
  124: { name: 'OpenThermVersionMaster',        format: RegFormat.F88 },
  125: { name: 'OpenThermVersionSlave',         format: RegFormat.F88 },
  126: { name: 'MasterVersion',                 format: RegFormat.U8Pair },
  127: { name: 'SlaveVersion',                  format: RegFormat.U8Pair },
  128: { name: 'RemehadFdUcodes',               format: RegFormat.U8Pair },
  129: { name: 'RemehaServicemessage',          format: RegFormat.U8Pair },
  130: { name: 'RemehaDetectionConnectedSCU',   format: RegFormat.U8Pair },
};

/** Default poll IDs (mirrors the C++ defaultPollIds). */
export const DefaultPollIds: number[] = [
  0, 1, 5, 6, 14, 15, 17, 18, 25, 26, 27, 28, 48, 49, 56, 57,
  116, 120, 125, 127,
];

export type TDebugOtState = {
  /** Map of DataId (as decimal string key) -> raw uint16 value. */
  regs: Record<string, number>;
};

export type TDebugOtConfig = {
  /** List of DataIds to poll continuously. */
  ids: number[];
};

/** Format a raw uint16 value according to its register format. */
export function formatRegValue(raw: number, format: RegFormat): string {
  switch (format) {
    case RegFormat.F88: {
      const v = raw > 32767 ? raw - 65536 : raw; // treat as signed
      return (v / 256).toFixed(2);
    }
    case RegFormat.S16: {
      const v = raw > 32767 ? raw - 65536 : raw;
      return String(v);
    }
    case RegFormat.U16:
      return String(raw);
    case RegFormat.U8Pair: {
      const hi = (raw >> 8) & 0xff;
      const lo = raw & 0xff;
      return `${hi} / ${lo}`;
    }
    case RegFormat.S8Pair: {
      const hi = (raw >> 8) & 0xff;
      const lo = raw & 0xff;
      const shi = hi > 127 ? hi - 256 : hi;
      const slo = lo > 127 ? lo - 256 : lo;
      return `${shi} / ${slo}`;
    }
    case RegFormat.FlagPair: {
      const hi = (raw >> 8) & 0xff;
      const lo = raw & 0xff;
      return `0x${hi.toString(16).padStart(2, '0')} / 0x${lo.toString(16).padStart(2, '0')}`;
    }
    default:
      return `0x${raw.toString(16).padStart(4, '0')}`;
  }
}

/** Convert a user-entered string to a raw uint16 given the chosen format. */
export function parseRegValue(
  input: string,
  format: RegFormat
): number | undefined {
  const s = input.trim();
  if (!s) return undefined;
  switch (format) {
    case RegFormat.F88: {
      const f = parseFloat(s);
      if (isNaN(f)) return undefined;
      return Math.round(f * 256) & 0xffff;
    }
    case RegFormat.S16: {
      const n = parseInt(s, 10);
      if (isNaN(n)) return undefined;
      return (n + 0x10000) & 0xffff;
    }
    case RegFormat.U16:
    default: {
      const n = parseInt(s, 10);
      if (isNaN(n)) return undefined;
      return n & 0xffff;
    }
  }
}

/** Return true for the formats where a single numeric value is entered. */
export function isSingleValueFormat(format: RegFormat): boolean {
  return (
    format === RegFormat.F88 ||
    format === RegFormat.S16 ||
    format === RegFormat.U16
  );
}
