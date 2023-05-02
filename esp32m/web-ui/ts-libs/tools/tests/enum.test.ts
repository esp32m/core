import { enumTool } from '../src/enum';

enum Test {
  Zero,
  One,
  Two,
}

enum TestFlags {
  None = 0,
  First = 0x1,
  Second = 0x2,
  Third = 0x4,
}

test('cache tool instance', () => {
  expect(enumTool(Test) === enumTool(Test)).toEqual(true);
});

test('enumTool.contains()', () => {
  const tool = enumTool(Test);
  expect(tool.contains(undefined as unknown as Test)).toEqual(false);
  expect(tool.contains(Test.One)).toEqual(true);
  expect(tool.contains('One')).toEqual(true);
  expect(tool.contains(1)).toEqual(true);
  expect(tool.contains(-1)).toEqual(false);
});

test('enumTool.toNumber()', () => {
  const tool = enumTool(Test);
  expect(tool.toNumber(Test.One)).toStrictEqual(1);
  expect(tool.toNumber('One')).toStrictEqual(1);
  expect(tool.toNumber(1)).toStrictEqual(1);

  expect(tool.toNumber(undefined as unknown as Test)).toStrictEqual(undefined);
  expect(tool.toNumber(-1)).toStrictEqual(undefined);

  expect(() =>
    tool.toNumber(undefined as unknown as Test, { throw: true })
  ).toThrow(RangeError);
  expect(() => tool.toNumber(-1, { throw: true })).toThrow(RangeError);
});

test('enumTool.toOptions()', () => {
  const tool = enumTool(Test);
  expect(tool.toOptions()).toEqual([
    [0, 'Zero'],
    [1, 'One'],
    [2, 'Two'],
  ]);
});

test('enumTool.flagsToNames()', () => {
  const tool = enumTool(Test);
  expect(tool.bitsToNames(1n)).toEqual(['Zero']);
  expect(tool.bitsToNames(1)).toEqual(['Zero']);
  expect(tool.bitsToNames(2n)).toEqual(['One']);
  expect(tool.bitsToNames(2)).toEqual(['One']);
  expect(tool.bitsToNames(3n)).toEqual(['Zero', 'One']);
  expect(tool.bitsToNames(3)).toEqual(['Zero', 'One']);
  const a = [];
  for (let i = 3; i < 32; i++) a.push(i);
  expect(tool.bitsToNames(-1)).toEqual(['Zero', 'One', 'Two']);
  expect(tool.bitsToNames(-1, { missing: true })).toEqual([
    'Zero',
    'One',
    'Two',
    ...a,
  ]);
  for (let i = 32; i < 64; i++) a.push(i);
  expect(tool.bitsToNames(-1n)).toEqual(['Zero', 'One', 'Two']);
  expect(tool.bitsToNames(-1n, { missing: true })).toEqual([
    'Zero',
    'One',
    'Two',
    ...a,
  ]);
  const tf = enumTool(TestFlags);
  expect(tf.bitsToNames(1, { mask: true })).toEqual(['First']);
  expect(tf.bitsToNames(3, { mask: true })).toEqual(['First', 'Second']);
});
