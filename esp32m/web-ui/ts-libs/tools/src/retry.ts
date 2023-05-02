import { ILogger } from '@ts-libs/log';
import { DiscreteStatus } from './status';

export type TRetryOptions = {
  delayMax: number;
  delayMin: number;
  attemptsMax: number;
  decay: number;
  name?: string;
  logger?: ILogger;
};

const defaultRetryOptions: TRetryOptions = {
  delayMax: 10000,
  delayMin: 1000,
  attemptsMax: Infinity,
  decay: 1.3,
};

/** @summary retry logic
 * @returns true to indicate success
 * @returns false to indicate failure
 * @returns nothing to indicate success/failure later by calling success() or failure() explicitly */
export type RetryFn = () => Promise<boolean | void> | boolean | void;

export const enum RetryStatus {
  Stopped,
  Running,
  Success,
  Failure,
}

export class Retry {
  readonly options: TRetryOptions;
  readonly state = new DiscreteStatus(RetryStatus.Stopped);
  constructor(fn: RetryFn, options?: Partial<TRetryOptions>) {
    this._fn = fn;
    this.options = Object.assign({}, defaultRetryOptions, options);
  }
  start() {
    if (!this.state.is(RetryStatus.Stopped)) return;
    this.state.set(RetryStatus.Running);
    return this.run();
  }
  stop() {
    if (this.state.is(RetryStatus.Stopped)) return;
    this.state.set(RetryStatus.Stopped);
    this.clearTimer();
    return this.whenComplete();
  }
  success() {
    if (this.state.is(RetryStatus.Stopped)) return;
    this._attempt = 0;
    this.state.set(RetryStatus.Success);
    this.clearTimer();
  }
  failure() {
    if (this.state.is(RetryStatus.Stopped)) return;
    this.state.set(RetryStatus.Failure);
    this.clearTimer();
    if (!this._pending)
      this._ti = setTimeout(() => this.run(), this.calcDelay());
  }
  private clearTimer() {
    if (!this._ti) return;
    clearTimeout(this._ti);
    this._ti = undefined;
  }
  private async run() {
    this.clearTimer();
    if (this.state.any(RetryStatus.Stopped, RetryStatus.Success)) return;
    const { attemptsMax } = this.options;
    if (attemptsMax)
      if (this._attempt >= attemptsMax) {
        this.log(`number of attempts exceeded ${attemptsMax}, giving up`);
        await this.stop();
        return;
      }
    this.state.set(RetryStatus.Running);
    if (this._attempt) this.log(`retrying, attempt #${this._attempt}`);
    this._attempt++;
    this._pending = (async () => {
      try {
        const result = await Promise.resolve(this._fn());
        if (result === true) this.success();
        if (result === false) this.failure();
        return result;
      } catch (e) {
        this.log(e.toString());
        this.failure();
      } finally {
        this._pending = undefined;
      }
    })();
    await this.whenComplete();
    if (this.state.is(RetryStatus.Failure)) {
      this.clearTimer();
      this._ti = setTimeout(() => this.run(), this.calcDelay());
    }
  }
  private calcDelay() {
    const { decay, delayMax, delayMin } = this.options;
    let delay = 0;
    if (this._attempt > 0) {
      delay = Math.min(delayMax, delayMin * Math.pow(decay, this._attempt - 1));
    }
    return delay;
  }
  private whenComplete() {
    // this will never throw, see run()
    return Promise.resolve(this._pending);
  }
  private log(msg: string) {
    this.options.logger?.warn(
      `${this.options.name ? this.options.name + ': ' : ''}${msg}`
    );
  }
  private readonly _fn: RetryFn;
  private _pending?: Promise<boolean | void>;
  private _ti?: ReturnType<typeof setTimeout>;
  private _attempt = 0;
}
