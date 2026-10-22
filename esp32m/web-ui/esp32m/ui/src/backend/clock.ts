/** Server uptime projected using the browser's monotonic clock. */
export class ServerClock {
  constructor(private readonly monotonicNow = () => performance.now()) { }

  readonly subscribe = (listener: VoidFunction) => {
    this.listeners.add(listener);
    this.schedule();
    return () => {
      this.listeners.delete(listener);
      if (!this.listeners.size) this.stop();
    };
  };
  readonly getSnapshot = () => this.value;
  readonly getServerSnapshot = () => undefined;
  get generation() { return this.epoch; }
  now() { return this.estimate(); }

  observe(serverTimeMs: unknown) {
    if (typeof serverTimeMs !== 'number' || !Number.isFinite(serverTimeMs) || serverTimeMs < 0) return;
    const local = this.monotonicNow();
    // Delayed envelopes must not move the projected clock backwards.
    this.anchorServer = Math.max(serverTimeMs, this.estimate(local) ?? serverTimeMs);
    this.anchorLocal = local;
    this.tick();
    this.schedule();
  }
  reset() {
    ++this.epoch;
    this.anchorServer = undefined;
    this.value = undefined;
    this.stop();
    this.notify();
  }
  private readonly listeners = new Set<VoidFunction>();
  private epoch = 0;
  private anchorServer?: number;
  private anchorLocal = 0;
  private value?: number;
  private timer?: ReturnType<typeof setTimeout>;
  private estimate(local = this.monotonicNow()) {
    return this.anchorServer === undefined ? undefined : this.anchorServer + Math.max(0, local - this.anchorLocal);
  }
  private tick() {
    this.value = this.estimate();
    this.notify();
  }
  private notify() { for (const listener of this.listeners) listener(); }
  private schedule() {
    if (this.timer !== undefined || !this.listeners.size || this.anchorServer === undefined) return;
    this.timer = setTimeout(() => {
      this.timer = undefined;
      this.tick();
      this.schedule();
    }, 1000);
  }
  private stop() {
    clearTimeout(this.timer);
    this.timer = undefined;
  }
}

export const sampleAge = (sampledAtMs: unknown, serverTimeMs?: number) =>
  typeof sampledAtMs === 'number' && Number.isFinite(sampledAtMs) && sampledAtMs >= 0 &&
    serverTimeMs !== undefined && sampledAtMs <= serverTimeMs
    ? Math.max(0, serverTimeMs - sampledAtMs) : -1;
