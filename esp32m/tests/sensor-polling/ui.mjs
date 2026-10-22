// Production shared controls: acknowledgment, offline read-back, no duplicate
// writes, and MAQI settings remaining reachable without any sensor sample.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
const core = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(path.join(core, 'web-ui/package.json'));
const babel = require('@babel/core');
const compiled = {};
for (const file of ['DeviceControl.tsx', 'maqi136/Content.tsx']) {
  const filename = path.join(core, 'web-ui/esp32m/ui/src/dev', file);
  compiled[file] = (await babel.transformAsync(readFileSync(filename, 'utf8'), {
    filename, configFile: false, babelrc: false,
    presets: [[require.resolve('@babel/preset-typescript'), { onlyRemoveTypeImports: false }],
      [require.resolve('@babel/preset-react'), { runtime: 'automatic', development: false }]],
    plugins: [require.resolve('@babel/plugin-transform-modules-commonjs')],
  })).code;
}
let hooks = [], cursor = 0, dirty = false, effects = [];
let server = { _disabled: false }, failure, hold, queries = 0, refreshes = 0;
const calls = [];
const slot = initial => {
  const i = cursor++;
  if (!(i in hooks)) hooks[i] = initial();
  return i;
};
const react = {
  useState(initial) {
    const i = slot(() => typeof initial === 'function' ? initial() : initial);
    return [hooks[i], value => {
      const next = typeof value === 'function' ? value(hooks[i]) : value;
      if (!Object.is(next, hooks[i])) { hooks[i] = next; dirty = true; }
    }];
  },
  useRef(initial) { return hooks[slot(() => ({ current: initial }))]; },
  useEffect(effect, deps) {
    const i = slot(() => undefined);
    if (!hooks[i] || deps.some((d, n) => !Object.is(d, hooks[i][n]))) {
      hooks[i] = deps;
      effects.push(effect);
    }
  },
};
const api = {
  async request(name, command, data) {
    calls.push({ name, command, data: JSON.parse(JSON.stringify(data)) });
    if (hold) await hold;
    if (failure === 'reject') throw new Error('Rejected');
    server = { _disabled: data._disabled };
    if (failure === 'lost') throw new Error('Lost response');
    return { data: server };
  },
};
const jsx = (type, props) => ({ type, props: props ?? {} });
const modules = {};
function load(file) {
  if (modules[file]) return modules[file];
  const exports = modules[file] = {};
  vm.runInNewContext(`(function(exports, require) {${compiled[file]}\n})`, { console })(exports, id => {
    if (id === 'react') return react;
    if (id === 'react/jsx-runtime') return { jsx, jsxs: jsx };
    if (id === '@mui/material' || id === '@mui/icons-material') return new Proxy({}, { get: (_, key) => key });
    if (id === '@ts-libs/tools') return { errorToString: e => e.message };
    if (id === '@ts-libs/ui-app') return { CardBox: 'CardBox' };
    if (id === '../../app') return { NameValueList: 'NameValueList' };
    if (id === '../..') return { useModuleState: () => undefined, useSampleAge: () => -1, useBackendApi: () => api };
    if (id === './types') return { Name: 'MAQI-136' };
    if (id === '../DeviceControl') return load('DeviceControl.tsx');
    if (id === '../backend') return {
      useBackendApi: () => api,
      useSubscribedState: (name, options) => {
        assert.equal(options.view, 'device');
        ++queries;
        return [server, async () => { ++refreshes; return server; }, true];
      },
    };
    throw new Error(`Unexpected dependency ${id}`);
  });
  return exports;
}
const shared = load('DeviceControl.tsx');
function render(fn = () => shared.useDeviceControl('test-device')) {
  for (let pass = 0; pass < 10; ++pass) {
    dirty = false; cursor = 0;
    const result = fn();
    for (const effect of effects.splice(0)) effect();
    if (!dirty) return result;
  }
  throw new Error('Render did not settle');
}
function nodes(root) {
  const result = [];
  function walk(node) {
    if (Array.isArray(node)) return node.forEach(walk);
    if (!node || typeof node !== 'object') return;
    if (typeof node.type === 'function') return walk(node.type(node.props));
    result.push(node);
    walk(node.props.action); walk(node.props.control); walk(node.props.children);
  }
  walk(root); return result;
}
let control = render();
assert.equal(control.known, true);
assert.equal(control.disabled, false);
let release;
hold = new Promise(resolve => { release = resolve; });
const pending = control.setDisabled(true);
await control.setDisabled(true);
assert.equal(calls.length, 1);
control = render();
assert.equal(control.disabled, false); // Never optimistically claim silence.
assert(nodes(shared.DeviceDisableSetting({ control })).find(n => n.type === 'Checkbox').props.disabled);
release(); await pending; hold = undefined;
control = render();
assert.equal(control.disabled, true);
assert.deepEqual(calls[0], { name: 'test-device', command: 'config-set', data: { _disabled: true } });
assert(shared.DeviceDisabledAlert({ control }));
assert.equal(refreshes, 1);
failure = 'lost';
await control.setDisabled(false);
control = render();
assert.equal(control.disabled, false); // Lost ACK recovered with a local read.
assert.match(control.error, /Lost response/);
assert.equal(calls.length, 2); // No blind write retry.
failure = 'reject';
await control.setDisabled(true);
control = render();
assert.equal(control.disabled, false);
assert.match(control.error, /Rejected/);
server = { _disabled: true }; // Restored config or another browser changed it.
control = render();
assert.equal(control.disabled, true);
hooks = []; effects = [];
const maqi = load('maqi136/Content.tsx');
const view = nodes(render(() => maqi.Content({})));
assert(view.some(n => n.type === 'CardBox'));
assert(view.some(n => n.type === 'IconButton' && n.props['aria-label'] === 'Configure MAQI-136'));
assert(view.some(n => n.type === 'Alert' && String(n.props.children).includes('disabled in settings')));
assert(queries > 0);
console.log('Device UI: confirmed toggle, duplicate suppression, lost ACK read-back, errors, restored state and offline MAQI settings passed');
