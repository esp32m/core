export const Name = 'rs485-health';
export type Counts = {
  attempts: number;
  success: number;
  errors: number;
  timeouts: number;
  invalid: number;
  other: number;
  unverified: number;
  health: number | null;
  errorRate: number | null;
  timeoutRate: number | null;
  invalidRate: number | null;
  otherRate: number | null;
  meanMs: number | null;
  successMs: number | null;
  maxMs: number | null;
};
export type Peer = {
  kind: 'modbus' | 'raw' | 'overflow';
  address: number;
  lastAttemptAtMs: number;
  recent: Counts | null;
  recentAvailable: boolean;
  total: Counts;
};
export type State = {
  windowSeconds: number;
  windowMs: number;
  windowEndedAtMs: number;
  comparisonStartedAtMs: number;
  session: number;
  lastAttemptAtMs: number;
  publisherRunning: boolean;
  requestsPerSecond: number | null;
  recent: Counts;
  total: Counts;
  peers: Peer[];
};

export const value = (n: number | null | undefined, digits = 1, suffix = '') =>
  typeof n === 'number' && Number.isFinite(n) ? `${n.toFixed(digits)}${suffix}` : '—';
export const peerName = (peer: Peer) => peer.kind === 'raw' ? 'Raw protocols (SM10 / vendor)'
  : peer.kind === 'overflow' ? 'Additional addresses' : `Modbus ${peer.address}`;
export const score = (n: number | null | undefined) => value(n, 2, '%');
export const expired = (receivedAt: number, now: number) => now - receivedAt > 5000;
