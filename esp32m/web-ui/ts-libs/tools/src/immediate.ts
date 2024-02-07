export function immediatePromise<F extends (this: any, ...args: any[]) => any>(
  fn: F
) {
  return new Promise((resolve, reject) =>
    setImmediate(async () => {
      try {
        resolve(await Promise.resolve(fn()));
      } catch (e) {
        reject(e);
      }
    })
  );
}
