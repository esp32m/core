import { isBoolean, isFunction, isUndefined } from './is-type';

type TestFunction<T> = (i: T) => boolean;

type TOptions<T> = {
  start?: true;
  end?: true;
  test: TestFunction<T>;
};

export function trim<T>(a: T[], options?: TOptions<T>): T[] {
  if (!a.length) return a;
  let start = true;
  let end = true;
  let test: TestFunction<T> = isUndefined;
  if (options) {
    if (isBoolean(options.start) || isBoolean(options.end)) {
      start = !!options.start;
      end = !!options.end;
    }
    if (isFunction<TestFunction<T>>(options.test)) test = options.test;
  }
  let si = 0;
  let ei = a.length;
  if (start) while (si < ei && test(a[si])) si++;
  if (end) while (si < ei && test(a[ei - 1])) ei--;
  return si > 0 || ei < a.length ? a.slice(si, ei) : a;
}
