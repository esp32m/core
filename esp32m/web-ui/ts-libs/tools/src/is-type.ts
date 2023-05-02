export const isUndefined = (v: unknown): v is undefined =>
  typeof v == 'undefined';

export const isString = (v: unknown): v is string => typeof v == 'string';

export const isNumber = (v: unknown): v is number => typeof v == 'number';

export const isBoolean = (v: unknown): v is boolean => typeof v == 'boolean';

export const isFunction = <T extends CallableFunction>(v: unknown): v is T =>
  typeof v == 'function';

export const isObject = (v: unknown): v is Record<any, any> =>
  typeof v == 'object';

export const isPlainObject = (v: unknown): v is Record<any, any> => {
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
