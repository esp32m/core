export const withTimeout = <T>(
  p: Promise<T>,
  timeout: number,
  where?: string
): Promise<T> =>
  new Promise<T>(async (resolve, reject) => {
    const th = setTimeout(() => {
      reject(new Error(`timeout${where ? ' in ' + where : ''}`));
    }, timeout);
    try {
      resolve(await p);
    } catch (e) {
      reject(e);
    } finally {
      clearTimeout(th);
    }
  });
