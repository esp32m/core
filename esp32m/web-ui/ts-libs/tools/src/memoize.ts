function shallowEqual(
  prev: Array<any> | undefined,
  next: Array<any> | undefined
) {
  if (prev === next) return true;
  if (!Array.isArray(prev) || !Array.isArray(next)) return false;
  if (prev.length != next.length) return false;
  for (let i = 0; i < prev.length; i++) if (prev[i] !== next[i]) return false;
  return true;
}

export function memoizeLast<F extends (this: any, ...args: any[]) => any>(
  fn: F,
  comparer?: (
    prev: Parameters<F> | undefined,
    next: Parameters<F> | undefined
  ) => boolean
) {
  let lastResult: ReturnType<F> | undefined = undefined;
  let lastArgs: Parameters<F> | undefined = undefined;
  const equalityChecker = comparer || shallowEqual;
  return function (this: any, ...args: Parameters<F>): ReturnType<F> {
    if (!equalityChecker(lastArgs, args)) {
      lastArgs = args;
      lastResult = fn.call(this, ...args);
    }
    return lastResult as ReturnType<F>;
  };
}
