import { TChannel, TDialogRead, TParameter, TRegister } from './types';
import { ParameterCatalog } from './catalog';

export const decodeParameter = (
  register?: TRegister
): TParameter | undefined => {
  const descriptor = ParameterCatalog.find(
    (p) => p.address === register?.address
  );
  if (!descriptor || !register) return undefined;
  // Only live readings cross the wire; labels and edit limits stay in the UI.
  return {
    ...descriptor,
    raw: register.raw,
    error: register.error,
    skipped: register.skipped,
    cached: register.cached,
  };
};

export const decodeParameters = (
  registers: TRegister[],
  cached = false
): TParameter[] =>
  ParameterCatalog.map((descriptor) => {
    const matches = registers.filter((r) => r.address === descriptor.address);
    return decodeParameter(
      matches.length === 1
        ? { ...matches[0], cached }
        : {
            address: descriptor.address,
            skipped: true,
          }
    )!;
  });

export const BaudRates = [2400, 4800, 9600, 19200, 38400, 57600, 115200];
export const Groups: Array<[string, string]> = [
  ['F0', 'Pump control, PID and pressure'],
  ['F1', 'Motor and soft-start'],
  ['F2', 'Protections'],
  ['F3', 'Multi-pump and RS485'],
  ['F4', 'Analog inputs, X1 and relay'],
];

type TimedChannel = TChannel & { age: number };
export const fresh = (channel?: TimedChannel) =>
  !!channel?.valid && channel.age >= 0 && channel.age <= 10_000;

export const reading = (channel: TimedChannel | undefined, digits: number) =>
  fresh(channel) && typeof channel?.value === 'number'
    ? channel.value.toFixed(digits)
    : '--';

export const rawText = (raw: number) =>
  `${raw} (0x${raw.toString(16).toUpperCase().padStart(4, '0')})`;

export const hasRaw = (
  value: TRegister
): value is TRegister & { raw: number } =>
  Number.isInteger(value.raw) &&
  value.raw! >= 0 &&
  value.raw! <= 65535 &&
  !value.skipped &&
  value.error === undefined;

export const dialogReadWarning = (result: TDialogRead, values: TRegister[]) => {
  const received = values.filter(hasRaw).length;
  if (!result.aborted && received === values.length) return undefined;
  const reasons = {
    failures: 'Read stopped after three consecutive communication failures.',
    deadline: 'Read stopped because its time limit was reached.',
    disabled: 'Read stopped because the device was disabled.',
    changed: 'Read stopped because the device state changed.',
  };
  const reason = result.aborted
    ? (result.abortReason && reasons[result.abortReason]) ||
      'Read stopped before all registers were attempted.'
    : 'Some registers could not be read.';
  return `${reason} ${received} of ${values.length} values received. Refresh to try again.`;
};

export const registerError = (value?: TRegister) => {
  if (value?.skipped) return 'Not attempted because the scan stopped';
  if (value?.error === 263) return 'Response timed out';
  if (value?.error === 259) return 'Device or read operation was not ready';
  return String(value?.error ?? 'No response');
};

export const canEdit = (parameter: TParameter) =>
  parameter.writable &&
  ![0x000c, 0x0307, 0x0308, 0x0309, 0x030c].includes(parameter.address) &&
  (!!parameter.options?.length ||
    (typeof parameter.scale === 'number' && parameter.scale > 0));

export const editable = (parameter: TParameter) =>
  canEdit(parameter) && hasRaw(parameter) && !parameter.cached;

export const numericValue = (parameter: TParameter, raw: number) => {
  const signed = parameter.signed && raw >= 32768 ? raw - 65536 : raw;
  return Number((signed * parameter.scale!).toFixed(8));
};

export const parameterInput = (parameter: TParameter) =>
  hasRaw(parameter)
    ? String(
        parameter.options?.length
          ? parameter.raw
          : numericValue(parameter, parameter.raw)
      )
    : '';

export const parameterText = (parameter: TParameter) => {
  if (parameter.skipped) return 'Not read';
  if (
    !hasRaw(parameter) &&
    !(parameter.cached && Number.isInteger(parameter.raw))
  )
    return 'Unavailable';
  if (parameter.options?.length)
    return (
      parameter.options.find((option) => option.value === parameter.raw)
        ?.label || `Unknown (${parameter.raw})`
    );
  if (parameter.scale === undefined) return `Raw ${rawText(parameter.raw!)}`;
  return `${parameter.inferred ? 'Approx. ' : ''}${numericValue(parameter, parameter.raw!)}${parameter.unit ? ` ${parameter.unit}` : ''}`;
};

export const encodeInput = (
  parameter: TParameter | undefined,
  input: string
): { raw?: number; error?: string } => {
  if (!parameter || !editable(parameter))
    return { error: 'Parameter is read-only.' };
  if (!/^-?\d+(\.\d+)?$/.test(input)) return { error: 'Enter a number.' };
  const value = Number(input);
  if (!Number.isFinite(value)) return { error: 'Enter a finite number.' };
  if (parameter.options?.length)
    return parameter.options.some((option) => option.value === value)
      ? { raw: value }
      : { error: 'Select a supported value.' };
  if (parameter.min !== undefined && value < parameter.min)
    return {
      error: `Minimum: ${parameter.min}${parameter.unit ? ` ${parameter.unit}` : ''}`,
    };
  if (parameter.max !== undefined && value > parameter.max)
    return {
      error: `Maximum: ${parameter.max}${parameter.unit ? ` ${parameter.unit}` : ''}`,
    };
  const counts = value / parameter.scale!;
  const rounded = Math.round(counts);
  if (Math.abs(counts - rounded) > 1e-7)
    return {
      error: `Use increments of ${parameter.scale}${parameter.unit ? ` ${parameter.unit}` : ''}.`,
    };
  if (
    parameter.step &&
    Math.abs(value / parameter.step - Math.round(value / parameter.step)) > 1e-7
  )
    return {
      error: `Use increments of ${parameter.step}${parameter.unit ? ` ${parameter.unit}` : ''}.`,
    };
  if (
    rounded < (parameter.signed ? -32768 : 0) ||
    rounded > (parameter.signed ? 32767 : 65535)
  )
    return { error: 'Value is outside the register range.' };
  return { raw: rounded < 0 ? rounded + 65536 : rounded };
};

const FaultNames: Record<number, string> = {
  0: 'No fault record',
  1: 'E0C1: Overcurrent during acceleration',
  2: 'E0C2: Overcurrent during deceleration',
  3: 'E0C3: Overcurrent at constant speed',
  4: 'EHU1: Overvoltage during acceleration',
  5: 'EHU2: Overvoltage during deceleration',
  6: 'EHU3: Overvoltage at constant speed',
  7: 'EHU4: Overvoltage while stopped',
  8: 'ELU0: Undervoltage while running',
  9: 'ESC1: Power module fault',
  10: 'E-OH: Heatsink overtemperature',
  11: 'EOL1: Inverter overload',
  12: 'EOL2: Motor overload',
  13: 'E-EF: External device fault',
  14: 'ECPU: CPU fault',
  15: 'E-oS: PID feedback lost / sensor fault',
  16: 'E485: RS485 communication fault',
  17: 'ETUN: Pipe burst (reserved)',
  18: 'ECCF: Current detection fault',
  19: 'EEEP: EEPROM read/write fault',
  20: 'EPLI: Input phase loss',
  21: 'E-LT: Allowed running time exceeded',
  22: 'EPLO: Output phase loss',
  24: 'E-LP: Water shortage',
};

export const FaultRegisters: Array<[number, string]> = [
  [18, 'Most recent fault'],
  [17, 'Second most recent fault'],
  [16, 'Third most recent fault'],
  [19, 'Frequency at fault'],
  [20, 'Current at fault'],
  [21, 'DC-bus voltage at fault'],
  [22, 'Inputs at fault'],
  [23, 'Outputs at fault'],
  [24, 'Drive status at fault'],
  [25, 'Temperature at fault'],
];

export const faultText = (index: number, register?: TRegister) => {
  if (register?.skipped) return 'Not read';
  if (!register || !hasRaw(register)) return 'Unavailable';
  const { raw } = register;
  if (index <= 18) return `${FaultNames[raw] || 'Unknown fault'} (${raw})`;
  if (index === 19) return `Approx. ${(raw * 0.01).toFixed(2)} Hz`;
  if (index === 20) return `Approx. ${(raw * 0.1).toFixed(1)} A`;
  if (index === 21) return `Approx. ${raw} V`;
  if (index === 25) return `Approx. ${(raw * 0.1).toFixed(1)} \u00b0C`;
  if (index === 24) {
    const stage = [
      'Zero speed',
      'Acceleration',
      'Deceleration',
      'Constant speed',
    ][(raw >> 8) & 3];
    const source = [
      'Panel source',
      'Communication source',
      'Reserved source',
      'Unknown source',
    ][(raw >> 12) & 3];
    const bus = [
      'Normal bus',
      'Undervoltage protection',
      'Overvoltage protection',
      'Unknown bus state',
    ][(raw >> 14) & 3];
    const flags: Array<[number, string]> = [
      [0x0004, 'Jog'],
      [0x0008, 'DC braking'],
      [0x0020, 'Overvoltage limiting'],
      [0x0040, 'Frequency reduction'],
      [0x0080, 'Overcurrent limiting'],
      [0x0400, 'Overload prewarning'],
    ];
    return (
      [
        raw & 1 ? 'RUN' : 'STOP',
        stage,
        source,
        bus,
        ...flags.filter(([mask]) => raw & mask).map(([, label]) => label),
        `Direction bit: ${(raw >> 1) & 1}`,
      ].join(', ') + ` (${rawText(raw)})`
    );
  }
  return rawText(raw);
};
