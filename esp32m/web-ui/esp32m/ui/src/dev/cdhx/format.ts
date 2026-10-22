export const value = (input: number | undefined, digits = 0, unit = '') =>
  Number.isFinite(input)
    ? `${(input as number).toFixed(digits)}${unit ? ` ${unit}` : ''}`
    : '—';

export const age = (valid: boolean, ageMs: number) =>
  valid ? value(ageMs / 1000, 1, 's') : 'No valid frame';

export const namedCode = (code?: number, name?: string) =>
  code === undefined ? '—' : name ? `${name} (${code})` : String(code);

export const byte = (input: number | null | undefined) =>
  input !== null &&
  input !== undefined &&
  Number.isInteger(input) &&
  input >= 0 &&
  input <= 255
    ? `0x${input.toString(16).toUpperCase().padStart(2, '0')}`
    : '—';
