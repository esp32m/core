export type TFlattenContext = {
  result: Record<string, any>;
  options: TFlattenOptions;
  path: string;
  value?: any;
};

type PassTypeOption = true | ((c: TFlattenContext) => void);

export type TFlattenOptions = {
  delimiter: string;
  path: string;
  passFunction?: PassTypeOption;
  passUndefined?: PassTypeOption;
};

const defaultFlattenOptions: TFlattenOptions = {
  delimiter: '.',
  path: '',
};

export function flatten(value: any, options?: Partial<TFlattenOptions>) {
  const eo = Object.assign({}, defaultFlattenOptions, options);
  const c: TFlattenContext = {
    result: {},
    options: eo,
    path: eo.path,
  };
  function pass(v: any, path: string, pto?: PassTypeOption) {
    if (pto === true) c.result[path] = v;
    else if (typeof pto === 'function') pto(c);
  }
  function process(v: any, path: string) {
    c.path = path;
    c.value = v;
    switch (typeof v) {
      case 'undefined':
        pass(v, path, c.options.passUndefined);
        break;
      case 'bigint':
      case 'boolean':
      case 'number':
      case 'string':
      case 'symbol':
        c.result[path] = v;
        break;
      case 'function':
        pass(v, path, c.options.passFunction);
        break;
      case 'object':
        if (v === null) {
          if (path) c.result[path] = v;
        } else
          for (const i in v) {
            const node = i.toString();
            process(v[i], path ? `${path}${c.options.delimiter}${node}` : node);
          }
        break;
    }
  }
  process(value, c.path);
  return c.result;
}

export function flatDiffKeys(
  a: any,
  b: any,
  options?: Partial<TFlattenOptions>
) {
  if (a === b) return [];
  const fa = flatten(a, options);
  const fb = flatten(a, options);
  const keys = new Set([...Object.keys(fa), ...Object.keys(fb)]);
  return [Array.from(keys).filter((k) => fa[k] !== fb[k]), fa, fb] as [
    string[],
    Record<string, any>,
    Record<string, any>
  ];
}
