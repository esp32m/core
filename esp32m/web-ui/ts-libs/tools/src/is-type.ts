export const isUndefined = (v: unknown): v is undefined =>
  typeof v == 'undefined';

export const isNullOrUndefined = (v: unknown): v is null | undefined =>
  v == null;

export const isString = (v: unknown): v is string => typeof v == 'string';

export const isNumber = (v: unknown): v is number => typeof v == 'number';

export const isBigint = (v: unknown): v is bigint => typeof v == 'bigint';

export const isBoolean = (v: unknown): v is boolean => typeof v == 'boolean';

export const isFunction = <T extends CallableFunction>(v: unknown): v is T =>
  typeof v == 'function';

export const isObject = (v: unknown): v is Record<any, any> =>
  typeof v == 'object';

export const isSymbol = (v: unknown): v is symbol => typeof v == 'symbol';

export const isPlainObject = <T extends object>(v: unknown): v is T => {
  if (!isObject(v)) return false;
  const ctor = v.constructor;
  if (ctor === undefined) return true;
  const prot = ctor.prototype;
  return isObject(prot) && prot.hasOwnProperty('isPrototypeOf');
};

export function isPrimitive(arg: any) {
  const type = typeof arg;
  return arg == null || (type != 'object' && type != 'function');
}

export function isThenable(arg: any): arg is Promise<any> {
  return typeof arg === 'object' && typeof arg.then === 'function';
}
