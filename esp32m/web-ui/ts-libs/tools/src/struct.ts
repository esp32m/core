import deepEqual from 'fast-deep-equal';
import { isUndefined } from './is-type';
import { murmur3 } from './murmur';

export type TStructFieldOptions = {
  array?: true;
};

export type TStructField = [
  name: string,
  children?: TStructFields,
  options?: TStructFieldOptions,
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
export type TDiffStructData = {
  [key: number | string]: TStructValue | TDiffStructData;
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
      return fields.reduce(
        (m, [name, children, options], i) => {
          const key = Array.isArray(data) ? i : name;
          if (data?.hasOwnProperty(key)) {
            const value = data[key];
            if (options?.array) {
              if (Array.isArray(value))
                m[name] = value.map((item) =>
                  children ? walk(children, item) : item
                );
              else if (value != null)
                throw new Error('array expected, got ' + JSON.stringify(value));
            } else m[name] = children ? walk(children, value) : value;
          }
          return m;
        },
        {} as Record<string, any>
      );
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
          else if (value != null)
            throw new Error('array expected, got ' + JSON.stringify(value));
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
  diff(prev: TUnfoldedStructData, next: TUnfoldedStructData): TDiffStructData {
    function containsRemoveKeysOnly(diffobj: TDiffStructData | TStructValue) {
      const keys = Object.keys(diffobj);
      return keys.length > 0 && keys.every((k) => parseInt(k) < 0);
    }
    function set(
      target: TDiffStructData,
      index: number,
      value: TDiffStructData | undefined
    ) {
      if (isUndefined(value)) target[-1 - index] = 0;
      else target[index] = value;
    }
    function walk(
      fields: TStructFields,
      prev: any,
      next: any
    ): TDiffStructData | TStructValue {
      return fields.reduce((md, [name, children, options], i) => {
        let p = prev?.[name];
        let n = next?.[name];
        const pu = isUndefined(p);
        const nu = isUndefined(n);
        if (p === n || (pu && nu)) return md;
        if (options?.array) {
          if (nu) {
            set(md, i, n);
            return md;
          }
          if (Array.isArray(n) && !n.length) {
            set(md, i, {});
            return md;
          }
          if (pu) p = [];
          if (nu) n = [];
          const pa = Array.isArray(p);
          const na = Array.isArray(n);
          if (!pa || !na)
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
          if (isUndefined(v)) set(md, i, v);
          else if (containsRemoveKeysOnly(v)) {
            if (nu) set(md, i, n);
            else md[i] = {};
          } else if (Object.keys(v).length) md[i] = v;
          else if (nu) set(md, i, n);
        } else if (!deepEqual(p, n)) set(md, i, n);
        return md;
      }, {} as TDiffStructData);
    }
    return walk(this.fields, prev, next) as TDiffStructData;
  }

  applyDiff(
    data: TUnfoldedStructData,
    diff: TDiffStructData
  ): TUnfoldedStructData {
    function walk(fields: TStructFields, data: any, diff: any): any {
      return fields.reduce(
        (map, [name, children, options], i) => {
          let prev = data?.[name];
          if (diff.hasOwnProperty(i)) {
            const next = diff[i];
            if (options?.array) {
              if (isUndefined(prev)) prev = [];
              if (!Array.isArray(prev))
                //  next will be an object even if data is array
                throw new Error('array expected, got ' + JSON.stringify(prev));
              const c = Math.max(
                prev.length,
                Object.keys(next)
                  .map((n) => Number(n) + 1)
                  .reduce((p, n) => (n > p ? n : p), 0)
              );
              const a = Array(c);
              for (let j = 0; j < c; j++)
                if (next.hasOwnProperty(j))
                  a[j] = children ? walk(children, prev[j], next[j]) : next[j];
                else if (prev.hasOwnProperty(j)) a[j] = prev[j];
              map[name] = a;
            } else
              map[name] =
                children && Object.keys(next).length
                  ? walk(children, prev, next)
                  : next;
          } else if (
            data &&
            data.hasOwnProperty(name) &&
            !diff.hasOwnProperty(-1 - i)
          )
            map[name] = prev;
          return map;
        },
        {} as Record<string, any>
      );
    }
    return walk(this.fields, data, diff);
  }
}
