export type CallOnce<F extends (...args: any[]) => any> = {
  (...args: Parameters<F>): ReturnType<F>;
  reset(): void;
  pending(): Promise<ReturnType<F> | void>;
};

type CallOnceInit = {
  resetOnResolve?: boolean;
  resetOnReject?: boolean;
  resetOnError?: boolean;
  resetOnSettled?: boolean;
};

export function callOnce<F extends (this: any, ...args: any[]) => any>(
  fn: F,
  options?: CallOnceInit
): CallOnce<F> {
  let result: ReturnType<F> | undefined;
  let error: unknown = undefined;
  let called = false;
  const reset = () => {
    called = false;
    error = undefined;
    result = undefined;
  };
  let pending: Promise<ReturnType<F> | void> = Promise.resolve();
  const { resetOnSettled = false } = options || {};
  const {
    resetOnReject = resetOnSettled,
    resetOnError = resetOnSettled,
    resetOnResolve = resetOnSettled,
  } = options || {};
  const callable = function (
    this: ThisParameterType<F>,
    ...args: Parameters<F>
  ) {
    if (!called)
      try {
        called = true; // reset() may be called from fn(), so this must come first
        result = fn.call(this, ...args);
        pending = Promise.resolve(result).then(
          (v) => {
            if (resetOnResolve) reset();
            return v;
          },
          (e) => {
            if (resetOnReject) reset();
            return e;
          }
        ) as ReturnType<F>;
      } catch (e) {
        error = e;
      }
    if (error) {
      if (resetOnError) reset();
      throw error;
    }
    return result as ReturnType<F>;
  };
  callable.pending = () => pending;
  callable.reset = reset;
  return callable;
}
