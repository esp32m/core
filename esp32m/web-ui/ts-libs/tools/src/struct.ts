import deepEqual from 'fast-deep-equal';
import { isUndefined } from './is-type';
import { murmur3 } from './murmur';

export type TStructFieldOptions = {
  array?: true;
};

export type TStructField = [
  name: string,
  children?: TStructFields,
  options?: TStructFieldOptions
];
export type TStructFields = Array<TStructField>;

type TStructValue = number | string | boolean;

type TUnfoldedStructItem =
  | TStructValue
  | TUnfoldedStructData
  | Array<TUnfoldedStructItem>;
export type TUnfoldedStructData = {
  [key: string]: TUnfoldedStructItem;
};

type TFoldedStructItem =
  | TStructValue
  | TFoldedStructData
  | Array<TFoldedStructItem>;

export type TFoldedStructData = Array<TFoldedStructItem>;
export type TDifftructData = {
  [key: number]: TStructValue | TDifftructData;
};
export class Struct {
  constructor(readonly fields: TStructFields) {}
  /**
   * Transform @param data into plain JS object structured according to @member fields
   * @param data - either plain JS object that follows @member fields structure (at least to some extend),
   * or an array where each item is a value of the field at the corresponding position
   * @returns plain JS object structured according to @member fields
   */
  unfold(data: TFoldedStructData | TUnfoldedStructData): TUnfoldedStructData {
    function walk(fields: TStructFields, data: any) {
      return fields.reduce((m, [name, children, options], i) => {
        const key = Array.isArray(data) ? i : name;
        if (data.hasOwnProperty(key)) {
          const value = data[key];
          if (options?.array)
            if (Array.isArray(value))
              m[name] = value.map((item) =>
                children ? walk(children, item) : item
              );
            else
              throw new Error('array expected, got ' + JSON.stringify(value));
          else m[name] = children ? walk(children, value) : value;
        }
        return m;
      }, {} as Record<string, any>);
    }
    return walk(this.fields, data);
  }

  /**
   * Transform @param data into array structured according to @member fields
   * @param data - either plain JS object that follows @member fields structure (at least to some extend),
   * or an array where each item is a value of the field at the corresponding position
   * @returns array structured according to @member fields
   */
  fold(data: TFoldedStructData | TUnfoldedStructData): TFoldedStructData {
    function walk(fields: TStructFields, data: any): any {
      return fields.map(([name, children, options], i) => {
        const key = Array.isArray(data) ? i : name;
        const value = data?.[key];
        if (options?.array)
          if (Array.isArray(value))
            return value.map((item) =>
              children ? walk(children, item) : item
            );
          else throw new Error('array expected, got ' + JSON.stringify(value));
        return children ? walk(children, value) : value;
      });
    }
    return walk(this.fields, data);
  }

  hash(data: TFoldedStructData | TUnfoldedStructData): number {
    const str = JSON.stringify(this.fold(data));
    const hash = murmur3(str);
    return hash;
  }

  /** diff two plain objects */
  diff(prev: TUnfoldedStructData, next: TUnfoldedStructData): TDifftructData {
    function walk(
      fields: TStructFields,
      prev: any,
      next: any
    ): Record<number, any> {
      if (isUndefined(prev)) return next;
      return fields.reduce((md, [name, children, options], i) => {
        const p = prev?.[name] || [];
        const n = next?.[name] || [];
        if (options?.array) {
          if (!Array.isArray(p) || !Array.isArray(n))
            throw new Error(
              `array expected, got ${JSON.stringify(p)} ${JSON.stringify(n)}`
            );
          const c = Math.max(p.length, n.length);
          const mda: Record<number, any> = {};
          for (let j = 0; j < c; j++) {
            const pj = p[j];
            const nj = n[j];
            if (children) {
              const v = walk(children, pj, nj);
              if (Object.keys(v).length) mda[j] = v;
            } else if (!deepEqual(pj, nj)) mda[j] = nj;
          }
          if (Object.keys(mda).length) md[i] = mda;
        } else if (children) {
          const v = walk(children, p, n);
          if (Object.keys(v).length) md[i] = v;
        } else if (!deepEqual(p, n)) md[i] = n;
        return md;
      }, {} as Record<number, any>);
    }
    return walk(this.fields, prev, next);
  }

  applyDiff(
    data: TUnfoldedStructData,
    diff: TDifftructData
  ): TUnfoldedStructData {
    function walk(fields: TStructFields, data: any, diff: any): any {
      return fields.reduce((map, [name, children, options], i) => {
        const prev = data?.[name];
        if (diff.hasOwnProperty(i)) {
          const next = diff[i];
          if (options?.array) {
            if (!Array.isArray(prev))
              //  next will be an object even if data is array
              throw new Error('array expected, got ' + JSON.stringify(prev));
            const c = Math.max(
              prev.length,
              Object.keys(next)
                .map((n) => Number(n))
                .reduce((p, n) => (n > p ? n : p), 0)
            );
            const a = Array(c);
            for (let j = 0; j < c; j++)
              if (next.hasOwnProperty(j))
                a[j] = children ? walk(children, prev[j], next[j]) : next[j];
              else if (prev.hasOwnProperty(j)) a[j] = prev[j];
            map[name] = a;
          } else map[name] = children ? walk(children, prev, next) : next;
        } else if (data && data.hasOwnProperty(name)) map[name] = prev;
        return map;
      }, {} as Record<string, any>);
    }
    return walk(this.fields, data, diff);
  }
}
