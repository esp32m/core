export const Name = 'modbus';
export type Options = { from: number; to: number; addr: number; cmd: number; regs: number; regc: number };
export type Bus = { configured: boolean; sharedBus: boolean; mode?: string; uart?: number; baud?: number; parity?: number };
export type Config = Options & Bus;
export type State = Config & {
  busy: boolean; job: number; operation: 'scan' | 'read' | 'write';
  scanned: number; total: number; progress: number; elapsedMs: number;
  cancelRequested: boolean; cancelled: boolean; error: number;
  results: Array<[number, number, number?]>; values?: number[];
  writeError?: number; verifyError?: number; verified?: boolean;
};
export type Input = { from: string; to: string; addr: string; regs: string; regc: string; data: string };
export const ReadCommands = [[1, 'Read coils (01)'], [2, 'Read discrete inputs (02)'],
  [3, 'Read holding registers (03)'], [4, 'Read input registers (04)']] as const;
export const WriteCommands = [[5, 'Write coil (05)'], [6, 'Write register (06)'],
  [15, 'Write coils (15)'], [16, 'Write registers (16)']] as const;
