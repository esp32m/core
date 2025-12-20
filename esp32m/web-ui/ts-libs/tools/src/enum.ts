import { isNumber, isUndefined } from './is-type';

type TNameOrValue = string | number;
export type TEnum = { [key: string]: TNameOrValue };
type TToNumberOptions = {
  throw?: true;
};
type TBitmapOptions = {
  /** if true - add bit numbers to the names array for bits with no corresponding name in the enum  */
  missing?: true;
  /** if true - this enum contains bit masks, otherwise - bit numbers. Ignored for bigints */
  mask?: true;
};

class Tool<T extends TEnum> {
  constructor(readonly enumType: T) { }
  contains(value: TNameOrValue) {
    if (isUndefined(value)) return false;
    const { enumType } = this;
    const r = enumType[value];
    return enumType[r] === value;
  }
  toNumber(value: TNameOrValue, options?: TToNumberOptions) {
    if (!isUndefined(value)) {
      const { enumType } = this;
      let r = enumType[value];
      if (!isUndefined(r)) {
        if (!isNumber(r)) r = enumType[r];
        if (isNumber(r)) return r;
      }
    }
    if (options?.throw)
      throw new RangeError(`value ${JSON.stringify(value)} is out of range`);
  }
  toName(value: TNameOrValue | undefined) {
    if (isUndefined(value)) return undefined;
    return this.enumType[value as keyof T];
  }
  names() {
    return Object.keys(this.enumType).filter(k => Number.isNaN(Number(k)));
  }
  toOptions() {
    return (
      this._options ||
      (this._options = Object.entries(this.enumType)
        .reduce(
          (out, [n, v]) => {
            if (isNumber(v)) out.push([v, n]);
            return out;
          },
          [] as Array<[number, string]>
        )
        .sort(([a], [b]) => a - b))
    );
  }
  bitsToNames(bitmap: number | bigint, options?: TBitmapOptions) {
    const result: Array<number | string> = [];
    let n = 0;
    const add = options?.missing
      ? (v: number) => result.push(this.enumType[v] || n)
      : (v: number) => {
        const name = this.enumType[v];
        if (name) result.push(name);
      };
    switch (typeof bitmap) {
      case 'bigint':
        while (bitmap && n < 64) {
          if (bitmap & 1n) add(n);
          n++;
          bitmap >>= 1n;
        }
        break;
      case 'number':
        while (bitmap) {
          if (bitmap & 1) add(options?.mask ? 1 << n : n);
          n++;
          bitmap >>>= 1;
        }
        break;
    }
    return result;
  }
  private _options?: Array<[number, string]>;
}

const tools = new Map<TEnum, Tool<any>>();

export function enumTool<T extends TEnum>(e: T): Tool<T> {
  if (!tools.has(e)) tools.set(e, new Tool(e));
  return tools.get(e) as Tool<T>;
}
