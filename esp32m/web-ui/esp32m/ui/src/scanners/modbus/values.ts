import type { Input } from './types';

export const integer = (text: string, min: number, max: number) => {
  if (!/^(?:0x[0-9a-f]+|\d+)$/i.test(text.trim())) return undefined;
  const n = Number(text.trim());
  return Number.isInteger(n) && n >= min && n <= max ? n : undefined;
};
export const requestFor = (tab: number, input: Input, cmd: number, confirm: boolean) => {
  const regs = integer(input.regs, 0, 65535);
  if (regs === undefined) return undefined;
  if (tab === 0) {
    const from = integer(input.from, 1, 247), to = integer(input.to, 1, 247);
    if (from === undefined || to === undefined || from > to || cmd < 1 || cmd > 4) return undefined;
    return { from, to, cmd, regs, regc: 1 };
  }
  const addr = integer(input.addr, 1, 247);
  const regc = integer(input.regc, 1, cmd === 16 ? 123 : 125);
  if (addr === undefined || regc === undefined || regs + regc > 65536) return undefined;
  if (tab === 1) return cmd >= 1 && cmd <= 4 ? { addr, cmd, regs, regc } : undefined;
  if (![5, 6, 15, 16].includes(cmd) || !confirm || ((cmd === 5 || cmd === 6) && regc !== 1))
    return undefined;
  const data = input.data.trim().split(/[\s,]+/).map(v => integer(v, 0, cmd === 5 || cmd === 15 ? 1 : 65535));
  if (data.length !== regc || data.some(v => v === undefined)) return undefined;
  return { addr, cmd, regs, regc, data: data as number[], confirm: true };
};
export const hex = (value: number) => '0x' + value.toString(16).toUpperCase().padStart(4, '0');
export const outcome = (error: number) => error === 0 ? 'Reply received' :
  error === 263 ? 'Timeout' : error === 264 ? 'Invalid response' : 'Error ' + error;
