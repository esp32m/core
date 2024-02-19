import { Struct, TStructFields } from '../src';

type TInnerStruct = {
  x?: number;
  y?: string;
};

type TStruct = {
  a?: string;
  b?: boolean;
  c?: TInnerStruct;
  d?: number;
  arrn?: Array<number>;
  arrs?: Array<TInnerStruct>;
};

const InnerStructFields: TStructFields = [['x'], ['y']];

const Fields: TStructFields = [
  ['a'],
  ['b'],
  ['c', InnerStructFields],
  ['d'],
  ['arrn', , { array: true }],
  ['arrs', [['x'], ['y']], { array: true }],
];

const InitialData: TStruct = {
  a: 'a',
  b: true,
  c: { x: 10, y: 'y' },
  d: 13,
  arrn: [100, 200, 300],
  arrs: [
    { x: 1, y: 'one' },
    { x: 3, y: 'three' },
    { x: 2, y: 'two' },
  ],
};

const InitialDataAsArray = [
  'a',
  true,
  [10, 'y'],
  13,
  [100, 200, 300],
  [
    [1, 'one'],
    [3, 'three'],
    [2, 'two'],
  ],
];

const struct = new Struct(Fields);
test('unfold', () => {
  let map = struct.unfold(InitialData);
  expect(map).toEqual(InitialData);
  map = struct.unfold(InitialDataAsArray);
  expect(map).toEqual(InitialData);
});

test('fold', () => {
  let arr = struct.fold(InitialData);
  expect(arr).toEqual(InitialDataAsArray);
  arr = struct.fold(InitialDataAsArray);
  expect(arr).toEqual(InitialDataAsArray);
});

describe('diff and apply', () => {
  const cases: Array<[TStruct, TStruct]> = [
    [{}, {}],
    [{ a: 'b' }, {}],
    [{ c: {} }, {}],
    [{ c: { x: 1 } }, {}],
    [{ c: { x: 1 } }, { c: {} }],
    [{ d: undefined }, { d: 1 }],
    [{ arrn: undefined }, { arrn: [] }],
    [{ arrn: undefined }, { arrn: [1] }],
  ];

  cases.forEach((c, i) =>
    test(`case ${i}`, () => {
      const diff = struct.diff(c[0], c[1]);
      /*if (i > 6) {
        console.log(`case ${i}: diff = ${JSON.stringify(diff)}`);
        debugger;
      }*/
      const applied = struct.applyDiff(c[0], diff);
      expect(applied).toStrictEqual(c[1]);
    })
  );

  test('single value', () => {
    const newValue = 'a-new';
    const changed = { ...InitialData, a: newValue };
    const diff = struct.diff(InitialData, changed);
    expect(diff).toEqual({ 0: newValue });
    const applied = struct.applyDiff(InitialData, diff);
    expect(applied).toEqual(changed);
  });
  test('inner struct value', () => {
    const newValue = 'y-new';
    const changed = { ...InitialData, c: { ...InitialData.c, y: newValue } };
    const diff = struct.diff(InitialData, changed);
    expect(diff).toEqual({ 2: { 1: newValue } });
    const applied = struct.applyDiff(InitialData, diff);
    expect(applied).toEqual(changed);
  });
  test('array value', () => {
    const newValue1 = 999;
    const newArrn = [...InitialData.arrn!];
    newArrn[1] = newValue1;
    const changed = { ...InitialData, arrn: newArrn };
    const diff = struct.diff(InitialData, changed);
    expect(diff).toEqual({ 4: { 1: newValue1 } });
    const applied = struct.applyDiff(InitialData, diff);
    expect(applied).toEqual(changed);
  });
});

describe('use case 1', () => {
  const prev = {
    active: 1,
  };
  const next = {
    active: 0,
  };
  const fields: TStructFields = [['active']];
  test('diff', () => {
    const struct = new Struct(fields);
    expect(struct.diff(prev, next)).toEqual({ '0': 0 });
  });
});

describe('use case 2', () => {
  type T = { active?: string };
  const prev: T = { active: 'yes' };
  const next: T = {};
  const fields: TStructFields = [['active']];
  test('diff', () => {
    const struct = new Struct(fields);
    const d = struct.diff(prev, next);
    expect(Object.keys(d).length).toEqual(1);
    expect(Object.keys(d)[0]).toEqual('-1');
    expect(d[-1]).toStrictEqual(0);
  });
});
