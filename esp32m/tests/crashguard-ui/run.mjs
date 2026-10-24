// Exercise the production notice, connection capability hook and global layout.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const core = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(path.join(core, 'web-ui/package.json'));
const babel = require('@babel/core');
const files = {
  recovery: 'web-ui/esp32m/ui/src/debug/crashguard/recovery.tsx',
  types: 'web-ui/esp32m/ui/src/debug/crashguard/types.ts',
  system: 'web-ui/esp32m/ui/src/system/types.ts',
  start: 'web-ui/esp32m/ui/src/esp32m.tsx',
  layout: 'web-ui/ts-libs/ui-navbar/src/layout.tsx',
};
const compiled = {};
for (const [name, file] of Object.entries(files)) {
  const filename = path.join(core, file);
  compiled[name] = (await babel.transformAsync(readFileSync(filename, 'utf8'), {
    filename, configFile: false, babelrc: false,
    presets: [[require.resolve('@babel/preset-typescript'), { onlyRemoveTypeImports: false }],
      [require.resolve('@babel/preset-react'), { runtime: 'automatic', development: false }]],
    plugins: [require.resolve('@babel/plugin-transform-modules-commonjs')],
  })).code;
}

const jsx = (type, props) => ({ type, props: props || {} });
let runner;
const sameDeps = (a, b) => a && b && a.length === b.length && a.every((v, i) => Object.is(v, b[i]));
const react = {
  createElement: (type, props, ...children) => jsx(type,
    children.length ? { ...props, children } : props),
  useState: initial => {
    const index = runner.cursor++;
    if (!(index in runner.slots)) runner.slots[index] = typeof initial === 'function' ? initial() : initial;
    const current = runner;
    return [current.slots[index], value => {
      const next = typeof value === 'function' ? value(current.slots[index]) : value;
      if (!Object.is(next, current.slots[index])) {
        current.slots[index] = next;
        current.dirty = true;
      }
    }];
  },
  useRef: initial => {
    const index = runner.cursor++;
    return runner.slots[index] ??= { current: initial };
  },
  useEffect: (effect, deps) => {
    const index = runner.cursor++;
    const previous = runner.slots[index];
    if (!previous || !sameDeps(previous.deps, deps)) {
      runner.slots[index] = { deps, cleanup: previous?.cleanup };
      runner.pending.push(() => {
        runner.slots[index].cleanup?.();
        runner.slots[index].cleanup = effect();
      });
    }
  },
};
const requests = [], watches = [], listeners = new Set();
let connection = 'connected', live = false, snapshot, route = 'home', registered = [];
const api = {
  status: {
    is: value => connection === value,
    changed: { subscribe: cb => {
      listeners.add(cb); return { unsubscribe: () => listeners.delete(cb) };
    } },
  },
  getInfo: target => new Promise((resolve, reject) => requests.push({ target, resolve, reject })),
};
const theme = { config: { breadcrumbs: false, header: { mobileHeight: 50, desktopHeight: 70 },
  navbar: { width: 200, collapseWidth: 70 } }, breakpoints: { up: () => '@large' },
  transitions: { create: () => '', duration: { shorter: 100 } } };
const styles = [];
const material = new Proxy({
  styled: type => style => {
    styles.push(style);
    return props => jsx(type, props);
  },
  useTheme: () => theme,
}, { get: (object, key) => object[key] || key });
const modules = {};
const context = vm.createContext({ console });
function load(name) {
  if (modules[name]) return modules[name];
  const exports = modules[name] = {};
  vm.runInContext(`(function(exports, require) {${compiled[name]}\n})`, context)(exports, id => {
    if (id === 'react') return react;
    if (id === 'react/jsx-runtime') return { jsx, jsxs: jsx };
    if (id === '@mui/material') return material;
    if (id === '@mui/icons-material') return { Home: 'Home' };
    if (id === '../../app') return { NameValueList: 'NameValueList' };
    if (id === '../../backend' || id === './backend') return {
      ConnectionStatus: { Connected: 'connected' },
      useBackendApi: () => api,
      useSubscribedState: (name, options) => {
        assert.equal(name, 'crash-guard');
        assert.equal(JSON.stringify(options.data), '{"view":"recovery"}');
        assert.equal(options.interval, undefined);
        watches.push(options.condition);
        return [snapshot, () => assert.fail('The banner must not poll or refresh manually'), live];
      },
      pluginUiBackend: () => ({ name: 'backend' }),
    };
    if (id === '../../system/types' || id === '../../system') return load('system');
    if (id === './types' && name === 'recovery') return load('types');
    if (id === './debug/crashguard/recovery') return load('recovery');
    if (id === '@ts-libs/plugins') return {
      register: (...plugins) => registered.push(...plugins),
      findPlugin: key => registered.find(plugin => plugin.name === key),
      getPlugins: () => registered,
    };
    if (id === '@ts-libs/ui-app') return { Root: 'Root', renderRoot: () => {} };
    if (id === '@ts-libs/ui-base') return { useNavPath: () => ({ segments: [] }) };
    if (id === '@ts-libs/ui-navbar') return {
      VerticalMenuHeader: 'VerticalMenuHeader', pluginUiNavbar: () => ({ name: 'navbar' }),
    };
    if (id === '@ts-libs/ui-snack') return { pluginUiSnack: () => ({ name: 'snack' }) };
    if (id === '@ts-libs/ui-i18n') return {
      pluginUi18n: () => ({ name: 'i18n' }), useTranslation: () => ({ t: value => value }),
    };
    if (id === './system') return { SystemSummary: 'SystemSummary' };
    if (id === './net') return { ApInfoBox: 'ApInfoBox', MqttStateBox: 'MqttStateBox', StaInfoBox: 'StaInfoBox' };
    if (id === './net/ota/plugin') return { OtaPlugin: { name: 'ota' } };
    if (id === './utils' && name === 'start') return { setDefine: () => {} };
    if (id === './utils' && name === 'layout') return { useMainMenuSections: () => undefined };
    if (id === 'react-router') return { Outlet: 'Outlet' };
    if (id === 'react-router-dom') return { NavLink: 'NavLink' };
    if (id === './context') return { useNavbarContext: () => ({ isCollapse: false, collapseClick: false }) };
    if (id === './Header') return { Header: 'Header' };
    if (id === './footer') return { Footer: 'Footer' };
    if (id === './vertical/Navbar') return { default: 'Navbar' };
    throw new Error(`Unexpected dependency ${id} from ${name}`);
  });
  return exports;
}

function makeRunner(component) {
  return { slots: [], cursor: 0, pending: [], dirty: true, nodes: [], text: '', component };
}
function render(current) {
  runner = current;
  runner.cursor = 0;
  runner.pending = [];
  runner.nodes = [];
  runner.dirty = false;
  function walk(node) {
    if (Array.isArray(node)) return node.map(walk).join(' ');
    if (node == null || typeof node === 'boolean') return '';
    if (typeof node !== 'object') return String(node);
    if (typeof node.type === 'function') return walk(node.type(node.props));
    runner.nodes.push(node);
    if (node.type === 'NameValueList') return walk(node.props.list);
    if (node.type === 'Collapse' && !node.props.in) return '';
    if (node.type === 'Outlet') return `Route: ${route}`;
    return walk(node.props.children);
  }
  runner.text = walk(componentCall(runner.component));
  for (const effect of runner.pending) effect();
  return runner;
}
const componentCall = component => component();
async function flush(current) {
  for (let i = 0; i < 10; ++i) {
    await Promise.resolve();
    if (current.dirty) render(current);
  }
  return current;
}
function disconnect() {
  connection = 'connecting'; live = false;
  for (const cb of listeners) cb();
}
function connect() {
  connection = 'connected';
  for (const cb of listeners) cb();
}
function unmount(current) {
  for (const slot of current.slots) slot?.cleanup?.();
}
const from = { slot: 1, ver: '1.2.3', sha256: 'deadbeef0123456789' };
const to = { slot: 0, ver: '1.2.3', sha256: '123456789abcdef0' };
const incident = { active: true, status: 'fallback', boot: 75, reason: 'unexpected-reset',
  resetReason: 4, checkpoint: 20, uptime: 1234, strike: 10, early: 3, from, to };
snapshot = { supported: true, current: to, recovery: incident };

let current = makeRunner(load('recovery').CrashGuardRecoveryNotice);
render(current);
assert.equal(current.text, '');
assert.equal(watches.at(-1), false);
assert.equal(requests.at(-1).target, 'app');
requests.at(-1).resolve({ data: {} }); // Old firmware has no capability field.
await flush(current);
assert.equal(watches.at(-1), false);
assert.equal(current.text, '');
disconnect(); await flush(current); connect(); await flush(current);
requests.at(-1).resolve({ data: { crashGuard: false } });
await flush(current);
assert.equal(watches.at(-1), false);
disconnect(); await flush(current); connect(); await flush(current);
requests.at(-1).resolve({ data: { crashGuard: true } });
await flush(current);
assert.equal(watches.at(-1), true);
assert.equal(current.text, ''); // Cache exists but fresh subscription is not live.
live = true; render(current);
assert.match(current.text, /switched to backup firmware/);
assert.match(current.text, /Running:  1\.2\.3 \(123456789abc\)/);
assert.match(current.text, /Affected firmware:  1\.2\.3 \(deadbeef0123\)/);
assert(!current.text.includes('Recovery boot')); // Details start collapsed.
assert.equal(current.nodes.find(n => n.type === 'Alert').props.onClose, undefined);
assert.equal(current.nodes.filter(n => n.type === 'Button').length, 1);
current.nodes.find(n => n.type === 'Button').props.onClick();
await flush(current);
assert.match(current.text, /Recovery boot 75/);
assert.match(current.text, /Panic \(4\)/);
assert.match(current.text, /1\.234 s/);
assert.equal(current.nodes.find(n => n.type === 'Button').props['aria-expanded'], true);

for (const recovery of [null, { ...incident, active: false },
  { ...incident, status: 'pending', active: false },
  { ...incident, status: 'inactive', active: false },
  { ...incident, status: 'resolved', active: false }]) {
  snapshot = { ...snapshot, recovery }; render(current);
  assert.equal(current.text, '');
}
snapshot = { supported: true, current: { slot: 1, ver: '1.2.4', sha256: 'feedfaceabcd9999' },
  recovery: { ...incident, status: 'testing' } };
render(current);
assert.match(current.text, /firmware is being evaluated/);
assert.match(current.text, /Running:  1\.2\.4 \(feedfaceabcd\)/);
assert(!current.text.includes('switched to backup firmware'));
snapshot = { ...snapshot, recovery: { ...incident, from: { ...from, ver: '', sha256: '' } } };
render(current);
assert.match(current.text, /Unknown version/);
disconnect(); await flush(current);
assert.equal(current.text, '');
connect(); await flush(current);
const staleProbe = requests.at(-1);
disconnect(); await flush(current); connect(); await flush(current);
const freshProbe = requests.at(-1);
staleProbe.resolve({ data: { crashGuard: true } });
await flush(current);
assert.equal(watches.at(-1), false); // Earlier connection cannot enable current cache.
freshProbe.resolve({ data: {} });
await flush(current);
assert.equal(watches.at(-1), false);
assert.equal(current.text, '');
disconnect(); await flush(current); connect(); await flush(current);
requests.at(-1).reject(new Error('unsupported'));
await flush(current);
assert.equal(watches.at(-1), false);
unmount(current);
assert.equal(listeners.size, 0);

load('start').startUi();
const plugin = registered.find(p => p.name === 'crashguard-recovery-notice');
assert(plugin);
assert.equal(plugin.use, undefined); // No optional Debug page dependency.
assert.equal(plugin.ui.content.notices[0], load('recovery').CrashGuardRecoveryNotice);
assert(!registered.find(p => p.name === 'crash-guard' || p.name === 'debug'));
snapshot = { supported: true, current: to, recovery: incident }; live = true;
current = makeRunner(load('layout').Layout);
render(current);
requests.at(-1).resolve({ data: { crashGuard: true } });
await flush(current);
for (const next of ['home', 'system', 'devices/meter']) {
  route = next; render(current);
  assert.match(current.text, /switched to backup firmware/);
  assert(current.text.indexOf('switched to backup firmware') < current.text.indexOf(`Route: ${next}`));
  assert.equal(current.nodes.filter(n => n.type === 'Alert').length, 1);
}
assert(styles.some(style => style({ theme }).flexDirection === 'column'));
unmount(current);
assert.equal(listeners.size, 0);
console.log('CrashGuard UI: capability absent/disabled/failure, compact subscription, live cache, fallback/testing identities, details, lifecycle, reconnect, default registration and global routes passed');
