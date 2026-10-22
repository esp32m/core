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
const filename = path.join(core, 'web-ui/esp32m/ui/src/dev/thermostat/Content.tsx');
const { code } = await babel.transformAsync(readFileSync(filename, 'utf8'), {
  filename, configFile: false, babelrc: false,
  presets: [[require.resolve('@babel/preset-typescript'), { onlyRemoveTypeImports: false }],
    [require.resolve('@babel/preset-react'), { runtime: 'automatic', development: false }]],
  plugins: [require.resolve('@babel/plugin-transform-modules-commonjs')],
});
const fixture = {
  config: { enabled: true, heat: true, shadow: false, targetC: 18, sensorUid: 'probe_temperature',
    strategy: 'automatic', heatHysteresisC: 1, stopHysteresisC: 1.5, proportionalBandC: 3,
    highTemperatureC: 35, minimumFlowC: 25, maximumFlowC: 65, minimumRunMs: 600000,
    minimumOffMs: 300000, lowPowerHoldMs: 60000, sensorFreshnessMs: 30000,
    feedbackFreshnessMs: 5000, commandTimeoutMs: 30000, allowFullShutdown: false },
  climate: { mode: 'heat', action: 'idle', temperature: 18, currentTemperature: 18.5, available: true },
  control: { ready: true, active: true, inhibited: false, reason: 'satisfied', phase: 'standby',
    strategy: 'native', backend: 'heater', fullShutdownSupported: true, nativeTemperatureSupported: true,
    flowSupported: false, pending: '', proposedCommand: 'none', lastCommand: '', lastResult: '' },
  source: { uid: 'probe_temperature', valid: true, sampledAtMs: 999500 },
  sources: [
    { uid: 'probe_temperature', name: 'Room probe', device: 'probe', enabled: true, valid: true, valueC: 18.5, sampledAtMs: 999500 },
    { uid: 'other_temperature', name: 'Other room', device: 'other', enabled: true, valid: false, valueC: null, sampledAtMs: 960000 },
    { uid: 'disabled_temperature', name: 'Disabled probe', device: 'disabled', enabled: false, valid: false, valueC: null, sampledAtMs: null },
  ],
};

function harness(modify = (state) => state) {
  const state = modify(structuredClone(fixture));
  const hooks = [];
  const calls = [];
  let cursor = 0;
  const jsx = (type, props) => ({ type, props: props || {} });
  const react = {
    useState(initial) { const i = cursor++; if (!(i in hooks)) hooks[i] = initial;
      return [hooks[i], value => { hooks[i] = typeof value === 'function' ? value(hooks[i]) : value; }]; },
    useRef(initial) { const i = cursor++; if (!(i in hooks)) hooks[i] = { current: initial }; return hooks[i]; },
  };
  const api = {
    async setState(name, data) { calls.push({ method: 'setState', name, data }); },
    async setConfig(name, data) { calls.push({ method: 'setConfig', name, data }); },
  };
  const exports = {};
  vm.runInNewContext(`(function(exports,require){${code}\n})`, { console })(exports, id => {
    if (id === 'react') return react;
    if (id === 'react/jsx-runtime') return { jsx, jsxs: jsx, Fragment: 'Fragment' };
    if (id === '@mui/material') return new Proxy({}, { get: (_, name) => name });
    if (id === '@ts-libs/ui-app') return { CardBox: 'CardBox' };
    if (id === '../..') return { NameValueList: 'NameValueList', useModuleState: () => state, useSampleAge: stamp => typeof stamp === "number" && stamp >= 0 ? 1000000 - stamp : -1 };
    if (id === '../../backend') return { useBackendApi: () => api };
    throw new Error(`Unexpected import ${id}`);
  });
  const render = () => { cursor = 0; return exports.default({ name: 'heater_thermostat' }); };
  return { state, calls, render };
}
function nodes(node, predicate) {
  if (!node || typeof node !== 'object') return [];
  if (Array.isArray(node)) return node.flatMap(item => nodes(item, predicate));
  return [...(predicate(node) ? [node] : []), ...nodes(node.props?.children, predicate)];
}
const byType = (tree, type) => nodes(tree, node => node.type === type);
const button = (tree, label) => byType(tree, 'Button').find(node => node.props.children === label);
const field = (tree, label) => byType(tree, 'TextField').find(node => node.props.label === label);
const settle = () => new Promise(resolve => setImmediate(resolve));

{
  const test = harness();
  let tree = test.render();
  assert(!button(tree, 'Heat').props.disabled);
  assert(!button(tree, 'Off').props.disabled);
  button(tree, 'Off').props.onClick();
  button(tree, 'Heat').props.onClick(); // Same-turn duplicate input is serialized.
  await settle();
  assert.equal(test.calls.length, 1);
  assert.deepEqual(JSON.parse(JSON.stringify(test.calls[0].data)), { mode: 'off' });
  tree = test.render(); button(tree, 'Settings').props.onClick(); tree = test.render();
  const sensor = field(tree, 'Room temperature sensor');
  assert.equal(sensor.props.value, 'probe_temperature');
  const options = byType(sensor, 'MenuItem');
  assert.equal(options.find(node => node.props.value === 'other_temperature').props.disabled, false);
  assert.equal(options.find(node => node.props.value === 'disabled_temperature').props.disabled, true);
  sensor.props.onChange({ target: { value: 'other_temperature' } });
  tree = test.render(); button(tree, 'Save').props.onClick(); await settle();
  assert.equal(test.calls[1].data.sensorUid, 'other_temperature');
  assert.equal(test.calls[1].method, 'setConfig');
}
{
  const test = harness(state => {
    state.control.inhibited = true; state.control.ready = false;
    state.control.reason = 'manual-takeover'; return state;
  });
  const tree = test.render();
  assert(button(tree, 'Heat').props.disabled);
  assert(!button(tree, 'Off').props.disabled);
  button(tree, 'Resume automatic control').props.onClick(); await settle();
  assert.deepEqual(JSON.parse(JSON.stringify(test.calls[0].data)), { resume: true });
}
{
  const test = harness(state => { state.config.sensorUid = 'missing_temperature'; state.config.shadow = true; return state; });
  let tree = test.render(); button(tree, 'Settings').props.onClick(); tree = test.render();
  assert(byType(field(tree, 'Room temperature sensor'), 'MenuItem').some(node => node.props.value === 'missing_temperature'));
  assert(byType(tree, 'Alert').some(node => String(node.props.children).includes('Observe only')));
  const fullShutdown = byType(tree, 'FormControlLabel').find(node => node.props.label === 'Full shutdown when satisfied');
  assert.equal(fullShutdown.props.control.props.checked, false);
}
{
  const test = harness(state => { state.control.flowSupported = true; return state; });
  let tree = test.render(); button(tree, 'Settings').props.onClick(); tree = test.render();
  assert(field(tree, 'Minimum boiler flow temperature (°C)'));
  assert.equal(field(tree, 'Minimum run time (minutes)'), undefined);
  assert(byType(tree, 'Typography').some(node => String(node.props.children).includes('keeps control of CH enable and hot water')));
}
console.log('thermostat UI: 4 groups passed');
