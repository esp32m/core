// Exercise the production card with deterministic state, clock and API hooks.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const require = createRequire(path.join(core, 'web-ui/package.json'));
const babel = require('@babel/core');
const compiled = {};
for (const file of ['types.ts', 'content.tsx']) {
  const filename = path.join(core, 'web-ui/esp32m/ui/src/debug/rs485', file);
  compiled[file] = (await babel.transformAsync(readFileSync(filename, 'utf8'), {
    filename, configFile: false, babelrc: false,
    presets: [[require.resolve('@babel/preset-typescript'), { onlyRemoveTypeImports: false }],
      [require.resolve('@babel/preset-react'), { runtime: 'automatic', development: false }]],
    plugins: [require.resolve('@babel/plugin-transform-modules-commonjs')],
  })).code;
}
const counters = (health = 100) => ({ attempts: 20, success: 20, errors: 0,
  timeouts: 0, invalid: 0, other: 0, unverified: 0, health, errorRate: 0,
  timeoutRate: 0, invalidRate: 0, otherRate: 0, meanMs: 40, successMs: 40, maxMs: 70 });
let state = { windowSeconds: 15, windowMs: 15000, windowEndedAtMs: 99980, comparisonStartedAtMs: 55000,
  session: 1, lastAttemptAtMs: 99600, publisherRunning: true, requestsPerSecond: 1.33,
  recent: counters(), total: counters(), peers: [
    {kind: 'modbus', address: 5, lastAttemptAtMs: 99600, recent: counters(50), total: counters(80)},
    {kind: 'raw', address: 0, lastAttemptAtMs: 99600, recent: counters(), total: counters()},
  ] };
let now = 100000, reference, refreshes = 0, resetSucceeds = true, live = true;
const requests = [];
const jsx = (type, props) => ({ type, props: props ?? {} });
const modules = {};
const context = vm.createContext({ console, Date: class extends Date { static now() { return now; } },
  setInterval: () => 1, clearInterval: () => {} });
function load(file) {
  if (modules[file]) return modules[file];
  const exports = modules[file] = {};
  vm.runInContext(`(function(exports, require) {${compiled[file]}\n})`, context)(exports, id => {
    if (id === 'react') return {
      useEffect: () => {}, useRef: initial => reference ??= {current: initial},
      useState: () => [now, () => {}],
    };
    if (id === 'react/jsx-runtime') return {jsx, jsxs: jsx};
    if (id === '@mui/material') return new Proxy({}, {get: (_, key) => key});
    if (id === '@ts-libs/ui-app') return {CardBox: 'CardBox'};
    if (id === '../../app') return {NameValueList: 'NameValueList'};
    if (id === '../../backend') return {
      useSampleAge: stamp => live && typeof stamp === "number" && stamp >= 0 ? now - stamp : -1,
      useSubscribedState: name => {
        assert.equal(name, 'rs485-health');
        return [state, async () => { ++refreshes; }, live];
      },
      useModuleApi: () => ({inProgress: 0, request: async command => {
        requests.push(command); return resetSucceeds ? {} : undefined;
      }}),
    };
    if (id === './types') return load('types.ts');
    throw new Error(`Unexpected dependency ${id}`);
  });
  return exports;
}
function render() {
  const nodes = [];
  function walk(node) {
    if (Array.isArray(node)) return node.map(walk).join(' ');
    if (node == null || typeof node === 'boolean') return '';
    if (typeof node !== 'object') return String(node);
    if (typeof node.type === 'function') return walk(node.type(node.props));
    nodes.push(node);
    if (node.type === 'NameValueList') return walk(node.props.list);
    return walk(node.props.action) + ' ' + walk(node.props.children);
  }
  const text = walk(load('content.tsx').content());
  return {text, nodes};
}
let view = render();
assert.match(view.text, /RS485|Recent health/);
assert.match(view.text, /50\.00%/); // Per-address failure isn't masked by aggregate.
assert.match(view.text, /Raw protocols/);
assert.match(view.text, /100\.00%/);
const completeState = state;
state = {...state, peers: [{...state.peers[0], recent: null, recentAvailable: false}]};
view = render();
assert.match(view.text, /per-address history is incomplete/);
assert.match(view.text, /Unavailable/);
assert(!view.text.includes('50.00%')); // Never turn a partial peer window into a score.
assert.match(view.text, /80\.00%/); // Exact lifetime data remains useful.
state = completeState;
view = render();
await view.nodes.find(n => n.type === 'Button').props.onClick();
assert.deepEqual(requests, ['reset']); assert.equal(refreshes, 1);
resetSucceeds = false;
await view.nodes.find(n => n.type === 'Button').props.onClick();
assert.equal(refreshes, 1); // Failed reset doesn't pretend to start a new trial.
now += 6000;
assert(!render().text.includes('updates are stale'));
live = false;
view = render();
assert.match(view.text, /updates are stale/);
assert(!view.text.includes('100.00%'));
assert(view.nodes.find(n => n.type === 'Button').props.disabled);
state = {...state, peers: [], recent: {...counters(null), attempts: 0, success: 0},
  total: {...counters(null), attempts: 0, success: 0}, windowMs: 0, comparisonStartedAtMs: now, session: 2};
live = true;
view = render();
assert.match(view.text, /Collecting the first completed second/);
assert(!view.text.includes('100.00%'));
state = {...state, windowMs: 15000};
assert.match(render().text, /No bus traffic/);
state = {...state, publisherRunning: false, total: {...state.total, unverified: 3}};
view = render();
assert.match(view.text, /InfluxDB metrics are not being updated/);
assert.match(view.text, /excluded from health scores/);
const helpers = load('types.ts');
assert.equal(helpers.score(null), '—');
assert.equal(helpers.score(NaN), '—');
assert.equal(helpers.score(0), '0.00%');
console.log('RS485 UI: per-address scores, reset success/failure, stale data, no traffic, warm-up and unverified replies passed');
