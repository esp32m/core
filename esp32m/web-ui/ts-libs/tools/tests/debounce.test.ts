import { debounce, identity, sleep } from '../src';

describe('debounce', () => {
  test('should debounce a function', (done) => {
    let callCount = 0;

    const debounced = debounce((value) => {
      ++callCount;
      return value;
    }, 32);

    const results = [debounced('a'), debounced('b'), debounced('c')];
    expect(results).toStrictEqual([undefined, undefined, undefined]);
    expect(callCount).toStrictEqual(0);

    setTimeout(() => {
      expect(callCount).toStrictEqual(1);

      const results = [debounced('d'), debounced('e'), debounced('f')];
      expect(results).toStrictEqual(['c', 'c', 'c']);
      expect(callCount).toStrictEqual(1);
    }, 128);

    setTimeout(() => {
      expect(callCount).toStrictEqual(2);
      done();
    }, 256);
  });

  test('subsequent debounced calls return the last `func` result', (done) => {
    const debounced = debounce(identity, 32);
    debounced('a');

    setTimeout(() => {
      expect(debounced('b')).not.toStrictEqual('b');
    }, 64);

    setTimeout(() => {
      expect(debounced('c')).not.toStrictEqual('c');
      done();
    }, 128);
  });

  test('should not immediately call `func` when `wait` is `0`', (done) => {
    let callCount = 0;
    const debounced = debounce(() => {
      ++callCount;
    }, 0);

    debounced();
    debounced();
    expect(callCount).toStrictEqual(0);

    setTimeout(() => {
      expect(callCount).toStrictEqual(1);
      done();
    }, 5);
  });

  test('should not call async `func` if it is running', async () => {
    const calls: Array<[number, number?]> = [];
    const debounced = debounce(async () => {
      const entry: [number, number?] = [Date.now()];
      calls.push(entry);
      await sleep(64);
      entry.push(Date.now());
    }, 32);

    debounced();
    await sleep(48);
    expect(calls.length).toStrictEqual(1);
    expect(calls[0][1]).toBeUndefined();
    // previous debounced is still running, should reschedule to 64+32-48+32=80
    debounced();
    await sleep(60);
    expect(calls.length).toStrictEqual(1);
    await debounced.scheduled();
    expect(calls.length).toStrictEqual(2);
  });
});
