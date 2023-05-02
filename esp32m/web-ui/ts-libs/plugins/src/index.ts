export type TPlugin = {
  readonly name: string;
  readonly use?: TPlugin | Array<TPlugin>;
};

const IndexKey = Symbol('plugin-index');

type RegisteredPlugin<T extends TPlugin> = {
  [IndexKey]: number;
} & T;

const plugins: {
  [name: string]: RegisteredPlugin<TPlugin>;
} = {};

let pluginsList: Array<TPlugin> | null = null;

let recursionCounter = 0;

function ensureRegistered(plugin: TPlugin) {
  const name = plugin?.name;
  if (!name) throw new Error('invalid use clause');
  if (plugins[name]) return;
  try {
    recursionCounter++;
    registerPlugin(plugin);
  } finally {
    recursionCounter--;
  }
}

export const registerPlugin = <T extends TPlugin>(
  plugin: T
): RegisteredPlugin<T> => {
  if (recursionCounter > 100) throw new Error('plugin recursion is too deep');
  if (!plugin) throw new Error('plugin cannot be empty');
  const { name, use } = plugin;
  if (!name) throw new Error('plugin must have a name');
  if (plugins[name]) throw new Error('duplicate plugin: ' + name);
  if (use)
    if (Array.isArray(use)) use.forEach(ensureRegistered);
    else ensureRegistered(use);
  const result = {
    ...plugin,
    [IndexKey]: Object.keys(plugins).length,
  };
  plugins[name] = result;
  pluginsList = null;
  return result;
};

export const register = (...p: Array<TPlugin>) => p.map(registerPlugin);

export const findPlugin = <T extends TPlugin>(name: string): T =>
  plugins[name] as unknown as T;

export const getPlugins = <T extends TPlugin>(
  predicate?: (p: T) => boolean
): Array<T> => {
  if (!pluginsList) {
    const plist = Object.values(plugins);
    plist.sort((a, b) => a[IndexKey] - b[IndexKey]);
    pluginsList = plist.map((p) => {
      const { [IndexKey]: _, ...rest } = p;
      return rest;
    });
  }
  let list = pluginsList as T[];
  if (predicate) list = list.filter(predicate);
  return list;
};
