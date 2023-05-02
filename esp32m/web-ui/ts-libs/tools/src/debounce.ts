import { noop } from './noop';

export type Debounce<F extends (...args: any[]) => any> = {
  (...args: Parameters<F>): ReturnType<F>;
  scheduled(): Promise<ReturnType<F>>;
  cancel(): void;
};

type DebounceInit = {
  onError?: (e: unknown) => void;
};

export function debounce<F extends (this: any, ...args: any[]) => any>(
  fn: F,
  wait: number,
  options?: DebounceInit
): Debounce<F> {
  let timerId: ReturnType<typeof setTimeout>;
  let running = false,
    reschedule = false;
  let toCall: () => ReturnType<F>;
  let result: ReturnType<F>;
  const scheduled: {
    promise: Promise<ReturnType<F>>;
    resolve: (arg: ReturnType<F>) => void;
    reject: (e: unknown) => void;
  } = {
    promise: Promise.resolve(undefined as ReturnType<F>),
    resolve: noop,
    reject: noop,
  };
  function cancel() {
    clearTimeout(timerId);
    scheduled.reject(new Error('cancelled'));
    reschedule = false;
  }
  function schedule() {
    scheduled.promise = new Promise<ReturnType<F>>((resolve, reject) => {
      scheduled.resolve = resolve;
      scheduled.reject = reject;
    });
    scheduled.promise.catch(noop);
    timerId = setTimeout(async () => {
      if (running) return;
      running = true;
      try {
        result = toCall();
        await Promise.resolve(result);
        scheduled.resolve(result);
      } catch (e) {
        scheduled.reject(e);
        options?.onError?.(e);
      } finally {
        running = false;
        if (reschedule) {
          reschedule = false;
          schedule();
        }
      }
    }, wait);
  }
  const callable = function (
    this: ThisParameterType<F>,
    ...args: Parameters<F>
  ) {
    cancel();
    toCall = fn.bind(this, ...args);
    if (running) reschedule = true;
    else schedule();
    return result;
  };
  callable.cancel = cancel;
  callable.scheduled = () => scheduled.promise;
  return callable;
}
