import { isUndefined } from './is-type';

export enum ArrayMergeMethod {
  Ovwerwrite,
  Union,
  Concat,
  Deep,
}

type TOptions = {
  arrays?: ArrayMergeMethod;
  isLeaf?: (element: any) => boolean;
};

const defaultOptions: TOptions = {
  arrays: ArrayMergeMethod.Deep,
};

export function merge(
  elements: Array<unknown>,
  options: TOptions = defaultOptions
): any {
  const isLeaf = (el: any) => options.isLeaf && options.isLeaf(el);
  function mergeArrays<T>(a: Array<T>, b: Array<T>) {
    switch (options.arrays) {
      case ArrayMergeMethod.Ovwerwrite:
        return b;
      case ArrayMergeMethod.Union:
        return Array.from(new Set(a.concat(b)));
      case ArrayMergeMethod.Concat:
        return a.concat(b);
      case ArrayMergeMethod.Deep:
        const l = Math.max(a.length, b.length);
        const result = Array<any>(l);
        for (let i = 0; i < l; i++) result[i] = mergeAny(a[i], b[i]);
        return result;
    }
  }
  const mergeObjects = <T extends Record<string, any>>(a: T, b: T) =>
    [...new Set([...Object.keys(a), ...Object.keys(b)])].reduce((o, k) => {
      o[k] = mergeAny(a[k], b[k]);
      return o;
    }, {} as Record<string, any>);

  function mergeAny(a: unknown, b: unknown) {
    if (isUndefined(b)) return a;
    if (isLeaf(b)) return b;
    switch (typeof a) {
      case 'object':
        if (a === null) return b;
        if (Array.isArray(a)) {
          if (!Array.isArray(b))
            throw new TypeError('cannot merge array with non-array');
          return mergeArrays(a, b);
        }
        if (typeof b !== 'object' || b === null) return b;
        //throw new TypeError('cannot merge object with non-object');
        return mergeObjects(a, b);
      default:
        return b;
    }
  }

  if (!Array.isArray(elements)) throw new TypeError('invalid argument');
  if (!elements.length) return elements;
  if (elements.every((e) => isUndefined(e) || Array.isArray(e)))
    return elements.reduce(
      (a, e) => mergeArrays(a as unknown[], e as unknown[]),
      []
    );
  return elements.reduce((a, e) => mergeAny(a, e), {});
}

export function cloneDeep<T>(value: T): T {
  return merge([value]) as T;
}
