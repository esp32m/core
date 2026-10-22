// Run the production TSX/helpers with deterministic hooks, timers and transport.
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
const ui = path.join(core, 'web-ui/esp32m/ui/src/dev/hsmwt10');
const compiled = new Map();
for (const file of ['Content.tsx', 'values.ts', 'types.ts']) {
  const filename = path.join(ui, file);
  const result = await babel.transformAsync(readFileSync(filename, 'utf8'), {
    filename, configFile: false, babelrc: false,
    presets: [
      [require.resolve('@babel/preset-typescript'), { onlyRemoveTypeImports: false }],
      [require.resolve('@babel/preset-react'), { runtime: 'automatic', development: false }],
    ],
    plugins: [require.resolve('@babel/plugin-transform-modules-commonjs')],
  });
  compiled.set(file, result.code);
}

const normalState = { address: 5, baud: 9600, valid: true, sampledAtMs: 999950,
  alarm: false, analog: 347, communicationPending: false, lastError: 0 };
const normalConfig = { address: 5, baud: 9600, sensitivity: 300,
  alarmDelay: 2, configReadError: 0 };
const tick = () => new Promise(resolve => setImmediate(resolve));
const plain = value => JSON.parse(JSON.stringify(value));

function harness(initial = {}, props = {}) {
  let state = { ...normalState, ...initial };
  let live = false;
  let config = { ...normalConfig };
  let now = 1_000_000;
  let frame, dirty = false, timerId = 0;
  const scopes = new Map(), timers = new Map(), effects = [];
  const calls = [], errors = [], handlers = new Map();
  const jsx = (type, props) => ({ type, props: props || {} });
  const depsEqual = (left, right) => left && right && left.length === right.length &&
    left.every((value, index) => Object.is(value, right[index]));
  const slot = initialValue => {
    const index = frame.cursor++;
    if (!(index in frame.hooks)) frame.hooks[index] = initialValue();
    return [frame.hooks, index];
  };
  const react = {
    Component: class {},
    createElement: (type, props, ...children) => jsx(type,
      { ...props, ...(children.length ? { children } : {}) }),
    useState(initialValue) {
      const [hooks, index] = slot(() => typeof initialValue === 'function' ? initialValue() : initialValue);
      return [hooks[index], value => {
        const next = typeof value === 'function' ? value(hooks[index]) : value;
        if (!Object.is(next, hooks[index])) { hooks[index] = next; dirty = true; }
      }];
    },
    useRef(initialValue) {
      const [hooks, index] = slot(() => ({ current: initialValue }));
      return hooks[index];
    },
    useEffect(effect, deps) {
      const [hooks, index] = slot(() => ({}));
      const current = hooks[index];
      if (!depsEqual(current.deps, deps)) {
        current.deps = deps;
        effects.push(() => { current.cleanup?.(); current.cleanup = effect(); });
      }
    },
    useMemo(factory, deps) {
      const [hooks, index] = slot(() => ({}));
      if (!depsEqual(hooks[index].deps, deps))
        hooks[index] = { deps, value: factory() };
      return hooks[index].value;
    },
    useCallback(callback, deps) { return react.useMemo(() => callback, deps); },
  };
  class FakeDate extends Date { static now() { return now; } }
  const api = {
    async getState(name) {
      calls.push({ method: 'getState', name });
      if (handlers.has('getState')) return handlers.get('getState')(name);
      return { data: state };
    },
    async request(name, command, data, options) {
      calls.push({ name, command, data: plain(data ?? {}), options });
      if (handlers.has(command)) return handlers.get(command)(data, options);
      if (command === 'config-get') return { data: { ...config } };
      if (command === 'config-set') {
        config = { ...config, ...data };
        state = { ...state, address: config.address, baud: config.baud };
        return { data: { ...config } };
      }
      throw new Error(`Unexpected command: ${command}`);
    },
  };
  const modules = {};
  const context = vm.createContext({ console, Date: FakeDate,
    setInterval(callback, interval) {
      const id = ++timerId;
      timers.set(id, { callback, interval, next: now + interval });
      return id;
    },
    clearInterval(id) { timers.delete(id); },
  });
  const load = filename => {
    if (modules[filename]) return modules[filename];
    const exports = {};
    modules[filename] = exports;
    const execute = vm.runInContext(`(function(exports, require) {${compiled.get(filename)}\n})`,
      context, { filename });
    execute(exports, id => {
      if (id === 'react') return react;
      if (id === '../DeviceControl') return {
        useDeviceControl: () => ({ disabled: false, busy: false, known: true }),
        DeviceDisabledAlert: () => null, DeviceDisableSetting: () => null,
      };
      if (id === 'react/jsx-runtime') return { jsx, jsxs: jsx, Fragment: 'Fragment' };
      if (id === '@mui/material' || id === '@mui/icons-material')
        return new Proxy({}, { get: (_, key) => key });
      if (id === '@ts-libs/ui-app') return { CardBox: 'CardBox' };
      if (id === '@ts-libs/tools') return { errorToString: error => error?.message ?? String(error) };
      if (id === '@ts-libs/ui-i18n') return { useTranslation: () => ({ tr: value => value }) };
      if (id === '@ts-libs/ui-snack') return { useSnackApi: () => ({ error: error => errors.push(error) }) };
      if (id === '../../app') return { NameValueList: 'NameValueList' };
      if (id === '../..') return { useBackendApi: () => api, useSubscribedState: () => [state, async () => state, live],
        useSampleAge: stamp => live && typeof stamp === 'number' && stamp >= 0 ? now - stamp : -1 };
      if (id === './types') return load('types.ts');
      if (id === './values') return load('values.ts');
      throw new Error(`Unexpected dependency: ${id}`);
    });
    return exports;
  };
  const component = load('Content.tsx');
  const invoke = (fn, props, key) => {
    const previous = frame;
    if (!scopes.has(key)) scopes.set(key, []);
    frame = { hooks: scopes.get(key), cursor: 0 };
    try { return fn(props); } finally { frame = previous; }
  };
  const text = node => {
    if (Array.isArray(node)) return node.map(text).join('');
    if (node == null || typeof node === 'boolean') return '';
    if (typeof node === 'object') {
      if (node.type === 'Dialog' && !node.props.open) return '';
      if (node.type === 'NameValueList') return text(node.props.list);
      return text(node.props?.children);
    }
    return String(node);
  };
  const all = () => {
    let result;
    for (let pass = 0; pass < 10; ++pass) {
      dirty = false;
      result = [];
      const visit = (node, key = 'root') => {
        if (Array.isArray(node)) return node.forEach((child, index) => visit(child, `${key}.${index}`));
        if (!node || typeof node !== 'object') return;
        if (typeof node.type === 'function') return visit(invoke(node.type, node.props, key), `${key}.child`);
        if (node.type === 'Dialog' && !node.props.open) return;
        result.push(node);
        visit(node.props.children, `${key}.children`);
        visit(node.props.action, `${key}.action`);
      };
      visit(invoke(component.Content, { name: 'wt10-test', ...props }, 'Content'));
      while (effects.length) effects.shift()();
      if (!dirty) return result;
    }
    throw new Error('Render did not settle after effects');
  };
  const find = (type, predicate = () => true) => {
    const found = all().find(node => node.type === type && predicate(node.props, node));
    assert(found, `Missing visible ${type}`);
    return found.props;
  };
  return {
    api, calls, errors, find, all, text,
    setState(update) { state = { ...state, sampledAtMs: now - 50, ...update }; live = true; },
    setProps(update) { props = { ...props, ...update }; live = false; },
    setLive(value) { live = value; },
    replaceState(value) { state = value; },
    setConfig(value) { config = value; },
    setHandler(command, handler) { handlers.set(command, handler); },
    clearHandler(command) { handlers.delete(command); },
    field(label) { return find('TextField', props => props.label === label); },
    button(label) { return find('Button', (_, node) => text(node) === label); },
    icon(label) { return find('IconButton', props => props['aria-label'] === label); },
    row(label) { return find('NameValueList').list.find(([name]) => name === label)?.[1]; },
    requests(command) { return calls.filter(call => call.command === command); },
    visibleText() { return all().map(text).join('\n'); },
    dialogOpen() { return all().some(node => node.type === 'Dialog'); },
    advance(ms) {
      const target = now + ms;
      while (true) {
        const next = [...timers.values()].filter(timer => timer.next <= target)
          .sort((a, b) => a.next - b.next)[0];
        if (!next) break;
        now = next.next;
        next.next += next.interval;
        next.callback();
      }
      now = target;
      all();
    },
    dispose() {
      for (const hooks of scopes.values())
        for (const hook of hooks) hook?.cleanup?.();
      assert.equal(timers.size, 0, 'unmount clears the local freshness timer');
    },
  };
}

const open = async h => { await h.icon('Configure HSM-WT10').onClick(); await tick(); };
const edit = (h, label, value) => h.field(label).onChange({ target: { value: String(value) } });

{
  const h = harness();
  assert.match(h.visibleText(), /Waiting for sensor data/,
    'a cached snapshot is not a newly verified reading after mount');
  assert.equal(h.row('Raw sensing value'), 'Unavailable');
  h.setState({});
  assert.match(h.visibleText(), /No leak detected/);
  assert.equal(h.row('Raw sensing value'), 347);
  assert.equal(h.calls.length, 0, 'normal rendering does not read configuration');
  h.setState({ alarm: true });
  assert.match(h.visibleText(), /Leak detected/);
  assert.equal(h.find('Alert', (_, node) => h.text(node) === 'Leak detected').severity, 'error');
  h.advance(6000);
  assert.match(h.visibleText(), /No recent sensor reading/);
  assert.equal(h.row('Raw sensing value'), 'Unavailable');
  assert.doesNotMatch(h.visibleText(), /No leak detected/);
  assert.equal(h.calls.length, 0, 'local freshness timer does not add network polling');
  h.setState({ alarm: false });
  assert.match(h.visibleText(), /No leak detected/, 'a new backend snapshot resets local age');
  h.setProps({ name: 'another-leak' });
  assert.match(h.visibleText(), /Waiting for sensor data/,
    'changing the module name resets the received-snapshot gate');
  h.setState({});
  assert.match(h.visibleText(), /No leak detected/);
  for (const update of [{ valid: false }, { sampledAtMs: -1 }, { sampledAtMs: 994999 },
    { sampledAtMs: NaN }, { alarm: null }, { analog: null }, { analog: Infinity },
    { communicationPending: true }]) {
    h.replaceState({ ...normalState, ...update });
    assert.match(h.visibleText(), /No recent sensor reading/);
    assert.equal(h.row('Raw sensing value'), 'Unavailable');
    assert.doesNotMatch(h.visibleText(), /No leak detected/);
  }
  h.replaceState(undefined);
  assert.match(h.visibleText(), /Waiting for sensor data/);
  h.dispose();
}

{
  const h = harness();
  await open(h);
  assert.equal(h.requests('config-get').length, 1);
  assert.equal(h.field('Modbus slave address').value, '5');
  assert.equal(h.field('Baud rate').value, '9600');
  assert.equal(h.field('Sensitivity').value, '300');
  assert.equal(h.field('Alarm delay (s)').value, '2');
  h.all(); h.all();
  assert.equal(h.requests('config-get').length, 1, 'configuration is on demand');
  await h.button('Refresh').onClick();
  assert.equal(h.requests('config-get').length, 2);
  assert.equal(h.requests('config-set').length, 0);
  assert(h.calls.every(call => call.name === 'wt10-test'), 'custom module name is used');
  h.button('Cancel').onClick();
  assert.equal(h.dialogOpen(), false);
  h.dispose();
}

{
  const h = harness();
  await open(h);
  edit(h, 'Alarm delay (s)', 3);
  for (const [label, valid, invalids] of [
    ['Modbus slave address', '5', ['', '0', '248', '253', '1.5', '1e1', '-1', 'NaN']],
    ['Baud rate', '9600', ['', '57600', '1200', '9600.5']],
    ['Sensitivity', '300', ['', '199', '501', '200.5', '2e2']],
    ['Alarm delay (s)', '3', ['', '-1', '65536', '0.5', '1e1']],
  ]) {
    for (const invalid of invalids) {
      edit(h, label, invalid);
      assert.equal(h.button('Save').disabled, true, `${label}: ${invalid}`);
    }
    edit(h, label, valid);
  }
  assert.equal(h.button('Save').disabled, false);
  h.dispose();
}

{
  const h = harness();
  await open(h);
  assert.equal(h.button('Save').disabled, true, 'no unchanged write');
  edit(h, 'Modbus slave address', 7);
  let finish;
  h.setHandler('config-set', async data => {
    await new Promise(resolve => { finish = resolve; });
    h.setConfig({ ...normalConfig, ...data });
    return { data };
  });
  const save = h.button('Save');
  const first = save.onClick();
  await save.onClick();
  assert.equal(h.requests('config-set').length, 1, 'immediate duplicate Save sends once');
  assert.deepEqual(h.requests('config-set')[0].data, { address: 7 });
  for (const label of ['Modbus slave address', 'Baud rate', 'Sensitivity', 'Alarm delay (s)'])
    assert.equal(h.field(label).disabled, true);
  assert.equal(h.button('Save').disabled, true);
  assert.equal(h.button('Refresh').disabled, true);
  assert.equal(h.button('Cancel').disabled, true);
  h.find('Dialog').onClose?.();
  assert.equal(h.dialogOpen(), true, 'backdrop close cannot discard an active write');
  finish();
  await first;
  await tick();
  assert.equal(h.requests('config-get').length, 2, 'one independent configuration read-back');
  assert.equal(h.calls.filter(call => call.method === 'getState').length, 2);
  assert.equal(h.dialogOpen(), false, 'verified success closes');
  h.dispose();
}

{
  for (const badConfig of [
    { ...normalConfig, sensitivity: undefined },
    { ...normalConfig, alarmDelay: undefined },
    { ...normalConfig, configReadError: 263 },
    { ...normalConfig, configReadError: undefined },
    { ...normalConfig, address: 248 },
    { ...normalConfig, baud: 57600 },
    { ...normalConfig, sensitivity: '300' },
    { ...normalConfig, alarmDelay: -1 },
    { ...normalConfig, pendingAddress: 7, pendingBaud: 9600 },
  ]) {
    const h = harness();
    h.setConfig(badConfig);
    await open(h);
    assert.equal(h.button('Save').disabled, true, JSON.stringify(badConfig));
    assert.equal(h.requests('config-set').length, 0);
    h.dispose();
  }
}

{
  const h = harness();
  await open(h);
  edit(h, 'Sensitivity', 400);
  h.setState({ communicationPending: true });
  assert.equal(h.button('Save').disabled, true, 'new pending telemetry locks an already open editor');
  assert.equal(h.field('Sensitivity').disabled, true);
  assert.equal(h.requests('config-set').length, 0);
  h.setState({ communicationPending: false });
  h.setHandler('config-get', async () => { throw new Error('configuration read timeout'); });
  await h.button('Refresh').onClick();
  assert.match(h.visibleText(), /configuration read timeout/);
  assert.equal(h.button('Save').disabled, true, 'a failed refresh discards an earlier editable baseline');
  assert.equal(h.field('Sensitivity').value, '');
  h.dispose();
}

{
  const h = harness();
  let finish;
  h.setHandler('config-get', async () => {
    await new Promise(resolve => { finish = resolve; });
    return { data: { ...normalConfig } };
  });
  const icon = h.icon('Configure HSM-WT10');
  const first = icon.onClick();
  await icon.onClick();
  await h.button('Refresh').onClick();
  assert.equal(h.requests('config-get').length, 1, 'duplicate open/refresh does not overlap requests');
  assert.equal(h.button('Save').disabled, true);
  assert.equal(h.button('Cancel').disabled, true);
  finish();
  await first;
  assert.equal(h.field('Sensitivity').value, '300');
  h.dispose();
}

{
  const h = harness();
  await open(h);
  edit(h, 'Modbus slave address', 7);
  edit(h, 'Baud rate', 19200);
  h.setHandler('config-set', async () => {
    // Backend verified the address before the following baud change failed.
    h.setConfig({ ...normalConfig, address: 7 });
    throw new Error('write timeout');
  });
  await h.button('Save').onClick();
  assert.equal(h.dialogOpen(), true);
  assert.match(h.visibleText(), /write timeout/);
  assert.equal(h.field('Modbus slave address').value, '7');
  assert.equal(h.field('Baud rate').value, '9600');
  assert.equal(h.requests('config-set').length, 1);
  assert.equal(h.requests('config-get').length, 2);
  assert.equal(h.calls.filter(call => call.method === 'getState').length, 2);
  assert.equal(h.button('Save').disabled, true, 'failed request reloads actual fields without scheduling a retry');
  h.all(); h.advance(5000);
  assert.equal(h.requests('config-set').length, 1);
  h.dispose();
}

{
  const h = harness();
  await open(h);
  edit(h, 'Sensitivity', 400);
  h.setHandler('config-set', async () => ({ data: {} }));
  await h.button('Save').onClick();
  assert.equal(h.dialogOpen(), true, 'acknowledged but unchanged value is not verified success');
  assert.equal(h.field('Sensitivity').value, '300');
  assert.equal(h.requests('config-set').length, 1);
  assert.equal(h.requests('config-get').length, 2);
  h.dispose();
}

{
  const h = harness();
  await open(h);
  edit(h, 'Alarm delay (s)', 4);
  h.setHandler('config-get', async () => { throw new Error('read-back timeout'); });
  await h.button('Save').onClick();
  assert.equal(h.dialogOpen(), true);
  assert.match(h.visibleText(), /read-back timeout/);
  assert.equal(h.button('Save').disabled, true, 'lost read-back blocks further edits');
  assert.equal(h.requests('config-set').length, 1);
  h.clearHandler('config-get');
  await h.button('Refresh').onClick();
  assert.equal(h.field('Alarm delay (s)').value, '4');
  assert.equal(h.requests('config-set').length, 1, 'recovery only reads');
  h.dispose();
}

{
  const h = harness();
  await open(h);
  edit(h, 'Modbus slave address', 7);
  h.setHandler('config-set', async () => {
    h.setConfig({ ...normalConfig, pendingAddress: 7, pendingBaud: 9600, configReadError: 259 });
    throw new Error('Address change not verified');
  });
  await h.button('Save').onClick();
  assert.equal(h.dialogOpen(), true);
  assert.equal(h.button('Save').disabled, true);
  assert.equal(h.requests('config-set').length, 1);
  assert.match(h.visibleText(), /awaiting verification|waiting for communication verification/i);
  h.dispose();
}

{
  const h = harness({}, { name: 'basement-leak', title: 'Basement leak' });
  assert.equal(h.find('CardBox').title, 'Basement leak');
  await h.icon('Configure Basement leak').onClick();
  assert(h.calls.every(call => call.name === 'basement-leak'));
  h.dispose();
}

{
  const h = harness();
  await open(h);
  edit(h, 'Modbus slave address', 7);
  edit(h, 'Baud rate', 19200);
  h.setHandler('config-set', async () => {
    h.setConfig({ ...normalConfig, address: 7 });
    h.setState({ address: 7 });
    throw new Error('Baud change not verified');
  });
  await h.button('Save').onClick();
  assert.equal(h.dialogOpen(), true,
    'the module card remains visible after its address changes');
  assert.match(h.visibleText(), /Baud change not verified/);
  assert.equal(h.field('Modbus slave address').value, '7');
  assert.equal(h.row('Modbus address'), 7);
  assert(h.calls.every(call => call.name === 'wt10-test'),
    'routing retains the stable module name after address migration');
  h.dispose();

  // Revisit the page with a cached snapshot at the newly migrated address.
  // The card and settings must remain accessible under the same module name.
  const revisited = harness({ address: 7 });
  revisited.setConfig({ ...normalConfig, address: 7 });
  assert.equal(revisited.find('CardBox').title, 'HSM-WT10');
  assert.equal(revisited.row('Modbus address'), 7);
  assert.match(revisited.visibleText(), /Waiting for sensor data/);
  revisited.setState({});
  assert.match(revisited.visibleText(), /No leak detected/);
  await open(revisited);
  assert.equal(revisited.field('Modbus slave address').value, '7');
  assert(revisited.calls.every(call => call.name === 'wt10-test'));
  assert.equal(revisited.requests('config-set').length, 0);
  revisited.dispose();
}

console.log('HSM-WT10 UI: 13 interaction groups passed (actual TSX/helpers, deterministic hooks/timers and transport).');
