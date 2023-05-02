const modes = ['QIO', 'QOUT', 'DIO', 'DOUT', 'FR', 'SR'];

export function flashMode(m: number): string {
  if (typeof m == 'number' && m >= 0 && m < modes.length) return modes[m];
  return '?';
}
