import { ILogger } from '@ts-libs/log';
import { isNumber } from './is-type';
import { TimeUnit, TimeValue } from './time';

interface TPeriodicOptions {
  logger?: ILogger;
  runOnStart?: boolean;
  maxRuns?: number;
  onerror?: (e: any) => void;
}

export class Periodic implements TPeriodicOptions {
  logger?: ILogger;
  runOnStart?: boolean;
  maxRuns?: number;
  onerror?: (e: any) => void;
  get timeout() {
    return this.#timeout;
  }
  set timeout(value: number | undefined) {
    const newValid = Periodic.isValidTimeout(value);
    if (newValid && !Periodic.isValidTimeout(this.#timeout))
      this.schedule(value);
    this.#timeout = newValid ? value : undefined;
  }
  get nextRun() {
    if (this.#due)
      return {
        at: this.#due,
        left: this.#due.minus(TimeValue.now()).toNumber(TimeUnit.Millisecond),
      };
  }
  get enabled() {
    return this.#enabled;
  }
  get runCount() {
    return this.#runCount;
  }
  constructor(
    readonly runnable: () => unknown,
    interval?: number,
    options?: TPeriodicOptions & { enabled?: boolean }
  ) {
    const { enabled, ...rest } = options || {};
    Object.assign(this, rest);
    if (enabled)
      this.enable(interval).catch((e) => this.logger?.warn(e, 'suspend'));
    else this.timeout = interval;
  }
  async enable(timeout?: number) {
    await this.whenComplete();
    if (Periodic.isValidTimeout(timeout)) this.#timeout = timeout;
    if (this.#enabled) await this.reset();
    else {
      this.#enabled = true;
      if (this.runOnStart) await this.run();
      else await this.reset();
    }
  }
  async disable() {
    this.#enabled = false;
    this.unschedule();
    await this.whenComplete();
  }
  async soonest(timeout: number) {
    if (Periodic.isValidTimeout(timeout)) {
      const { nextRun } = this;
      if (nextRun && nextRun.left < timeout) return;
      this.enable(timeout);
    } else this.enable();
  }
  suspend(duration: number) {
    // allows suspend to be called from within runnable
    setImmediate(() => {
      this.unschedule();
      this.startTimer(duration).catch((e) => this.logger?.warn(e, 'suspend'));
    });
  }
  async trigger() {
    if (!this.#enabled) return;
    await this.whenComplete();
    await this.run();
  }
  async reset() {
    this.#runCount = 0;
    if (!this.#enabled) return;
    this.unschedule();
    await this.startTimer();
  }
  private async run() {
    this.unschedule();
    if (this.#enabled) {
      this.#pending = (async () => {
        this.#runCount++;
        try {
          await Promise.resolve(this.runnable());
        } catch (e) {
          this.onerror?.(e);
          this.logger?.warn(e);
        } finally {
          this.#pending = undefined;
        }
      })();
    }
    await this.startTimer();
  }
  private async startTimer(timeout?: number) {
    await this.whenComplete();
    const interval = timeout || this.timeout;
    if (Periodic.isValidTimeout(interval)) this.schedule(interval);
  }
  private schedule(interval: number) {
    this.unschedule();
    if (
      !this.#enabled ||
      (isNumber(this.maxRuns) && this.#runCount >= this.maxRuns)
    )
      return;
    this.#timeoutHandle = setTimeout(() => this.run(), interval);
    this.#due = TimeValue.now().add(interval, TimeUnit.Millisecond);
  }
  private unschedule() {
    clearTimeout(this.#timeoutHandle);
    this.#due = undefined;
  }
  private async whenComplete() {
    if (this.#pending)
      // this will never throw, see run()
      await Promise.resolve(this.#pending);
  }
  #enabled = false;
  #pending?: Promise<void>;
  #timeout?: number;
  #timeoutHandle?: ReturnType<typeof setTimeout>;
  #due?: TimeValue;
  #runCount: number = 0;
  static readonly MaxTimeout = 2147483647;
  static isValidTimeout(value?: number): value is number {
    return isNumber(value) && value >= 0 && value <= Periodic.MaxTimeout;
  }
}
