import type { TConfig, TInputs, TSettings, TState } from './types';

export const BaudRates = [2400, 4800, 9600, 19200, 38400, 115200];

export const integer = (value: string, min: number, max: number) =>
  /^\d+$/.test(value) &&
  Number.isInteger(Number(value)) &&
  Number(value) >= min &&
  Number(value) <= max;

export const inputsFor = (config?: TConfig): TInputs => {
  const text = (value?: number) =>
    typeof value === 'number' && Number.isFinite(value) ? String(value) : '';
  return {
    address: text(config?.address),
    baud: text(config?.baud),
    sensitivity: text(config?.sensitivity),
    alarmDelay: text(config?.alarmDelay),
  };
};

export const validInputs = (input: TInputs) => ({
  address: integer(input.address, 1, 247),
  baud:
    integer(input.baud, 2400, 115200) && BaudRates.includes(Number(input.baud)),
  sensitivity: integer(input.sensitivity, 200, 500),
  alarmDelay: integer(input.alarmDelay, 0, 65535),
});

export const pending = (config?: TConfig) =>
  config?.pendingAddress !== undefined || config?.pendingBaud !== undefined;

export const configProblem = (config?: TConfig) => {
  if (!config) return 'Settings have not been read. Refresh to try again.';
  if (pending(config))
    return 'The communication change is awaiting verification. Refresh after communication recovers.';
  if (config.configReadError !== 0)
    return `Could not read all sensor settings${config.configReadError !== undefined ? ` (error ${config.configReadError})` : ''}. Refresh before saving.`;
  if (!Object.values(validInputs(inputsFor(config))).every(Boolean))
    return 'Incomplete or invalid sensor settings. Refresh before saving.';
  return undefined;
};

export const changesFor = (config: TConfig, input: TInputs) => {
  const changes: Partial<TSettings> = {};
  for (const key of Object.keys(input) as Array<keyof TSettings>) {
    const value = Number(input[key]);
    if (value !== config[key]) changes[key] = value;
  }
  return changes;
};

export const fresh = (state: TState | undefined, age: number) =>
  !!state?.valid &&
  !state.communicationPending &&
  Number.isFinite(age) &&
  age >= 0 &&
  age <= 5000 &&
  typeof state.alarm === 'boolean' &&
  typeof state.analog === 'number' &&
  Number.isFinite(state.analog);
