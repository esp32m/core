import { sleep } from './sleep';

type RateLimitedFn<T> = () => Promise<T>;

export class RateLimiter {
  constructor(readonly limitMs: number) {}
  private async whenComplete() {
    if (this.#pending)
      // this will never throw, see invoke()
      await Promise.resolve(this.#pending);
  }
  async invoke<T>(fn: RateLimitedFn<T>): Promise<T> {
    await this.whenComplete();
    const passed = Date.now() - (this.#last || 0);
    if (passed < this.limitMs) await sleep(this.limitMs - passed);
    const pending = (this.#pending = (async () => {
      try {
        return await Promise.resolve(fn());
      } finally {
        this.#last = Date.now();
        this.#pending = undefined;
      }
    })());
    return await pending;
  }
  #last?: number;
  #pending?: Promise<unknown>;
}
