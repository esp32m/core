import { Periodic, TPeriodicOptions } from '../src';

class Runner {
  readonly periodic: Periodic;
  readonly durations: number[] = [];
  constructor(interval: number, limitMs: number, opt?: TPeriodicOptions) {
    this._last = Date.now();
    this.periodic = new Periodic(
      () => {
        this.durations.push(Date.now() - this._last);
      },
      interval,
      opt
    );
    this._complete = new Promise<void>((resolve) => {
      setTimeout(() => {
        this.periodic.disable();
        resolve();
      }, limitMs);
    });
  }
  async complete() {
    await this._complete;
  }
  private _last: number;
  private _complete: Promise<void>;
}
test('initially disabled', async () => {
  const runner = new Runner(100, 200);

  expect(runner.periodic.enabled).toBeFalsy();
  await runner.complete();
  expect(runner.durations.length).toBe(0);
});

test('simple', async () => {
  const runner = new Runner(100, 1000, { enabled: true });
  setImmediate(() => expect(runner.periodic.enabled).toBeTruthy());
  await runner.complete();
  expect(runner.durations.length).toBeGreaterThan(8);
});
