import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const core = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(path.join(core, 'web-ui/package.json'));
const babel = require('@babel/core');
const toolkit = require('@reduxjs/toolkit');
const { Subject } = require('rxjs');
const compiled = {};
for (const file of ['client', 'subscriptions', 'types', 'state', 'hooks', 'clock']) {
  const filename = path.join(core, 'web-ui/esp32m/ui/src/backend', file + '.ts');
  compiled[file] = (await babel.transformAsync(readFileSync(filename, 'utf8'), {
    filename, configFile: false, babelrc: false,
    presets: [[require.resolve('@babel/preset-typescript'), { onlyRemoveTypeImports: false }]],
    plugins: [require.resolve('@babel/plugin-transform-modules-commonjs')],
  })).code;
}
const plain = value => value === undefined ? undefined : JSON.parse(JSON.stringify(value));
const settle = async () => { await Promise.resolve(); await Promise.resolve(); await Promise.resolve(); };

function harness() {
  let now = 1000, serial = 0, redux;
  const timers = new Map(), loaded = {}, sent = [], updates = [];
  const timeout = (fn, delay) => { const id = ++serial; timers.set(id, { fn, at: now + delay }); return id; };
  const clear = id => timers.delete(id);
  const advance = async ms => {
    const end = now + ms;
    for (;;) {
      const next = [...timers.entries()].filter(([, timer]) => timer.at <= end).sort((a,b) => a[1].at - b[1].at)[0];
      if (!next) break;
      now = next[1].at; timers.delete(next[0]); next[1].fn(); await settle();
    }
    now = end; await settle();
  };
  const tools = {
    DiscreteStatus: class { constructor(value) { this.value = value; this.changed = new Subject(); }
      set(value) { this.value = value; this.changed.next(value); } },
    isString: value => typeof value === 'string',
    isNumber: value => typeof value === 'number', noop: () => {},
    serializeError: error => ({ message: error?.message }),
    debounce: (fn, delay) => { let id; return () => { clear(id); id = timeout(fn, delay); }; },
  };
  const hooks = { cursor: 0, slots: [], effects: [] };
  const dependencies = (left, right) => left && right && left.length === right.length && left.every((v,i) => Object.is(v, right[i]));
  const react = {
    createContext: value => ({ value }), useContext: context => context.value,
    useMemo(fn, deps) { const i = hooks.cursor++; const old = hooks.slots[i];
      if (!old || !dependencies(old.deps, deps)) hooks.slots[i] = { deps, value: fn() }; return hooks.slots[i].value; },
    useCallback(fn, deps) { return this.useMemo(fn, deps); },
    useEffect(fn, deps) { const i = hooks.cursor++; const old = hooks.slots[i];
      if (!old || !dependencies(old.deps, deps)) hooks.effects.push(() => {
        old?.cleanup?.(); hooks.slots[i] = { deps, cleanup: fn() }; }); },
    useState(value) { const i = hooks.cursor++; if (!(i in hooks.slots)) hooks.slots[i] = typeof value === 'function' ? value() : value;
      return [hooks.slots[i], value => { hooks.slots[i] = typeof value === 'function' ? value(hooks.slots[i]) : value; }]; },
  };
  // Hooks are called as named imports, so do not rely on their receiver.
  react.useCallback = (fn, deps) => react.useMemo(() => fn, deps);
  react.useSyncExternalStore = (subscribe, snapshot) => {
    react.useEffect(() => subscribe(() => {}), [subscribe]);
    return snapshot();
  };
  const context = vm.createContext({ console, Date: class extends Date { static now() { return now; } },
    performance: { now: () => now },
    setTimeout: timeout, clearTimeout: clear, setInterval() { throw new Error('A state subscription must not poll'); }, clearInterval: clear });
  const load = file => {
    if (loaded[file]) return loaded[file];
    const exports = loaded[file] = {};
    vm.runInContext('(function(exports, require){' + compiled[file] + '\n})', context)(exports, id => {
      if (id === '@ts-libs/tools') return tools;
      if (id === '@reduxjs/toolkit') return toolkit;
      if (id === 'rxjs') return { Subject };
      if (id === 'reconnecting-websocket') return class {};
      if (id === './errors') return { deserializeEsp32mError: error => Object.assign(new Error(String(error)), { code: Array.isArray(error) ? error[0] : undefined }) };
      if (id === 'react') return react;
      if (id === 'react-redux') return { useSelector: selector => selector(redux), useDispatch: () => () => {} };
      if (id === '@ts-libs/ui-snack') return { useSnackApi: () => ({ error: () => {} }) };
      if (id === '@ts-libs/redux' || id === 'redux') return {};
      if (id.startsWith('./')) return load(id.slice(2));
      throw new Error('Unexpected import ' + id);
    });
    return exports;
  };
  const state = load('state'); redux = { backend: state.reducer(undefined, { type: 'init' }) };
  const ws = { send(data) { sent.push(...JSON.parse(data)); }, close() { this.onclose(); }, reconnect() { this.onopen(); } };
  const client = new (load('client').Client)(ws);
  client.stateUpdates.subscribe(update => { updates.push(plain(update)); redux = { backend: state.reducer(redux.backend, state.actions.stateUpdate(update)) }; });
  const receive = msg => ws.onmessage({ data: JSON.stringify(msg) });
  const response = (request, data, extra = {}) => receive({ type: 'response', seq: request.seq, name: request.name, source: request.target, data, ...extra });
  const ack = (request, state, stream = 'stream-' + request.data.target, rev = 1) =>
    response(request, { id: request.data.id, source: request.data.target ?? 'probe', stream, rev, state, serverTimeMs: now });
  const event = (source, data) => receive({ type: 'event', name: 'state-changed', source, data });
  const latest = name => sent.filter(request => request.name === name).at(-1);
  const render = (name = 'probe', options) => { hooks.cursor = 0; const result = load('hooks').useSubscribedState(name, options);
    for (const effect of hooks.effects.splice(0)) effect(); return result; };
  const renderAge = (stamp, name = 'probe', options) => { hooks.cursor = 0;
    const result = load('hooks').useSampleAge(stamp, name, options);
    for (const effect of hooks.effects.splice(0)) effect(); return result; };
  load('hooks').ClientContext.value = client;
  return { client, ws, sent, updates, receive, response, ack, event, latest, advance, load, render, renderAge,
    timerCount: () => timers.size,
    state: () => plain(redux), unmount: () => { hooks.slots.forEach(slot => slot?.cleanup?.()); hooks.slots = []; },
    open: () => ws.onopen(), close: () => ws.onclose() };
}

{
  const h = harness(); const module = h.client.module('probe');
  const release1 = module.useState(), release2 = module.useState();
  assert.equal(h.sent.length, 0); h.open();
  assert.equal(h.sent.length, 1); assert.equal(h.latest('state-subscribe').target, 'ui');
  h.ack(h.latest('state-subscribe'), { temperature: 18, diagnostic: { fault: false }, tuple: [1, null] });
  // A following event is processed in the same turn, before promise reactions.
  h.event('probe', { stream: 'stream-probe', baseRev: 1, rev: 2, patch: [
    { op: 'set', path: ['temperature'], value: 19 }, { op: 'remove', path: ['diagnostic', 'fault'] },
    { op: 'set', path: ['tuple'], value: [2, null, false] }] });
  assert.deepEqual(h.state().backend.modules.probe.state, { temperature: 19, diagnostic: {}, tuple: [2, null, false] });
  release1(); assert.equal(h.sent.length, 1); release1();
  release2(); assert.equal(h.latest('state-unsubscribe').data.id, h.latest('state-subscribe').data.id);
  h.response(h.latest('state-unsubscribe'), {}); await settle();
  const count = h.sent.length; await h.advance(60_000); assert.equal(h.sent.length, count);
  console.log('PASS shared consumers, atomic snapshot/event ordering, unsubscribe, no polling');
}
{
  const h = harness(); h.open();
  const releaseA = h.client.module('pins').useState({ pin: ['gpio', 4], feature: 1 });
  const requestA = h.latest('state-subscribe');
  const releaseSame = h.client.module('pins').useState({ feature: 1, pin: ['gpio', 4] });
  assert.equal(h.sent.length, 1);
  const releaseB = h.client.module('pins').useState({ pin: ['gpio', 5], feature: 1 });
  const requestB = h.latest('state-subscribe');
  h.ack(requestA, { value: 4 }, 'pin4'); h.ack(requestB, { value: 5 }, 'pin5');
  assert.equal(h.client.module('pins').stateSelector({ feature: 1, pin: ['gpio', 4] })(h.state()).value, 4);
  assert.equal(h.client.module('pins').stateSelector({ feature: 1, pin: ['gpio', 5] })(h.state()).value, 5);
  assert.equal(h.state().backend.modules.pins.state, undefined);
  releaseA(); releaseSame(); releaseB(); await settle();
  console.log('PASS canonical query identity and independent query caches');
}
{
  const h = harness(); h.open(); const module = h.client.module('probe'); const release = module.useState();
  const initial = h.latest('state-subscribe'); h.ack(initial, { value: 1 });
  h.event('probe', { stream: 'stream-probe', baseRev: 8, rev: 9, patch: [{ op: 'set', path: ['value'], value: 9 }] });
  assert.equal(h.latest('state-resync').data.id, initial.data.id);
  assert.equal(module.liveSelector()(h.state()), false);
  h.response(h.latest('state-resync'), { id: initial.data.id, source: 'probe', stream: 'stream-probe', rev: 9, state: { value: 9 } });
  assert.equal(module.liveSelector()(h.state()), true);
  assert.equal(h.state().backend.modules.probe.state.value, 9);
  const suspend1 = h.client.statePoller.suspend(), suspend2 = h.client.statePoller.suspend();
  assert.equal(module.liveSelector()(h.state()), false);
  const count = h.sent.length; suspend1(); suspend1(); assert.equal(h.sent.length, count);
  suspend2(); const resumed = h.latest('state-subscribe'); assert.notEqual(resumed.data.id, initial.data.id);
  h.ack(initial, { value: -1 }); assert.equal(h.state().backend.modules.probe.state.value, 9);
  h.ack(resumed, { value: 10 }, 'resumed');
  h.close(); assert.equal(module.liveSelector()(h.state()), false); await settle();
  h.open(); const reconnected = h.latest('state-subscribe'); assert.notEqual(reconnected.data.id, resumed.data.id);
  h.ack(reconnected, { value: 20 }, 'reconnected');
  h.event('probe', { stream: 'resumed', baseRev: 1, rev: 99, patch: [{ op: 'set', path: ['value'], value: -2 }] });
  assert.equal(h.state().backend.modules.probe.state.value, 20); release(); await settle();
  console.log('PASS gaps, resync, nested suspension, reconnect, obsolete stream rejection');
}
{
  const h = harness(); h.open(); const module = h.client.module('probe');
  const oldRead = h.client.request('probe', 'state-get'); await h.advance(100);
  const readRequest = h.latest('state-get'); const release = module.useState();
  h.ack(h.latest('state-subscribe'), { value: 10 });
  h.response(readRequest, { value: 1 }); await oldRead;
  assert.equal(h.state().backend.modules.probe.state.value, 10);
  const command = h.client.setState('probe', { value: 11 }); await h.advance(100);
  const commandRequest = h.latest('state-set');
  h.event('probe', { stream: 'stream-probe', baseRev: 1, rev: 2, patch: [{ op: 'set', path: ['value'], value: 12 }] });
  h.response(commandRequest, { value: 11 }); assert.equal((await command).data.value, 11);
  assert.equal(h.state().backend.modules.probe.state.value, 12);
  const read = h.client.getState('probe');
  assert.equal(h.latest('state-resync').data.id, h.latest('state-subscribe').data.id);
  h.response(h.latest('state-resync'), { id: h.latest('state-subscribe').data.id, source: 'probe', stream: 'stream-probe', rev: 3, state: { value: 13 } });
  assert.equal((await read).data.value, 13); assert.equal(h.state().backend.modules.probe.state.value, 13);
  release(); await settle();
  console.log('PASS reads/commands cannot roll back authoritative state and explicit reads resync');
}
{
  const h = harness(); h.open(); const module = h.client.module('probe');
  const release = module.useState(); const old = h.latest('state-subscribe'); release();
  const releaseNew = module.useState(); const current = h.latest('state-subscribe');
  h.ack(current, null, 'new'); h.ack(old, { obsolete: true }, 'old');
  assert.equal(h.state().backend.modules.probe.state, null);
  h.event('probe', { stream: 'new', baseRev: 1, rev: 2, patch: [{ op: 'set', path: [], value: [0, null] }] });
  assert.deepEqual(h.state().backend.modules.probe.state, [0, null]);
  h.event('probe', { stream: 'new', baseRev: 2, rev: 3, patch: [{ op: 'set', path: [1], value: { x: 1 } }] });
  assert.deepEqual(h.state().backend.modules.probe.state, [0, { x: 1 }]);
  const before = h.state().backend.modules.probe.state;
  h.event('probe', { stream: 'new', baseRev: 3, rev: 4, patch: [
    { op: 'set', path: [0], value: 100 }, { op: 'set', path: [1, '__proto__'], value: { polluted: true } }] });
  assert.deepEqual(h.state().backend.modules.probe.state, before);
  assert.equal({}.polluted, undefined); assert(h.latest('state-resync'));
  releaseNew(); await settle();
  console.log('PASS late acknowledgments, null/root/array patches, atomic rejection');
}
{
  const h = harness(); h.open();
  h.render('probe', { condition: false }); assert.equal(h.sent.length, 0);
  h.render('probe', { data: { b: 2, a: 1 } }); const initial = h.latest('state-subscribe');
  h.ack(initial, { value: 1 });
  const [state,,live] = h.render('probe', { data: { a: 1, b: 2 } });
  assert.equal(state.value, 1); assert(live); assert.equal(h.sent.length, 1);
  h.render('probe', { data: { a: 2 } }); assert.equal(h.sent.filter(r => r.name === 'state-subscribe').length, 2);
  assert.equal(h.sent.filter(r => r.name === 'state-unsubscribe').length, 1);
  h.unmount(); await settle();
  const once = harness(); once.open(); once.render('probe', { once: true, data: { pin: 4 } }); await once.advance(100);
  assert.equal(once.latest('state-get').data.pin, 4); assert(!once.latest('state-subscribe'));
  once.response(once.latest('state-get'), { value: 4 }); await settle();
  assert.equal(once.render('probe', { once: true, data: { pin: 4 } })[0].value, 4); once.unmount();
  console.log('PASS production hooks condition, canonical dependencies, query switch, once');
}
{
  const h = harness(); h.open(); const module = h.client.module('probe');
  const releaseState = module.useState(), regular = h.latest('state-subscribe');
  const releaseDevice = module.useState(undefined, 'device'), device = h.latest('state-subscribe');
  h.ack(regular, [1, 2]); h.ack(device, { _disabled: false }, 'device');
  assert.deepEqual(module.stateSelector()(h.state()), [1, 2]);
  assert.equal(module.stateSelector(undefined, 'device')(h.state())._disabled, false);
  releaseState(); releaseDevice(); await settle();
  console.log('PASS device metadata view is separate from legacy array state');
}
{
  const h = harness(); h.open(); const module = h.client.module('probe');
  const release = module.useState(); h.ack(h.latest('state-subscribe'), { value: 1 });
  await h.advance(14_999); assert.equal(h.latest('ping'), undefined);
  await h.advance(1); const ping = h.latest('ping'); assert.equal(ping.target, 'ui');
  h.response(ping, null); await settle();
  const resume = h.client.statePoller.suspend();
  const count = h.sent.filter(request => request.name === 'ping').length;
  await h.advance(30_000); assert.equal(h.sent.filter(request => request.name === 'ping').length, count);
  resume(); h.ack(h.latest('state-subscribe'), { value: 2 }, 'resumed');
  await h.advance(15_000); assert.equal(h.sent.filter(request => request.name === 'ping').length, count + 1);
  const before = h.latest('state-subscribe').data.id;
  await h.advance(10_000); // No response: reconnect obtains a new subscription snapshot.
  assert.notEqual(h.latest('state-subscribe').data.id, before);
  assert.equal(module.liveSelector()(h.state()), false);
  h.ack(h.latest('state-subscribe'), { value: 3 }, 'recovered');
  assert.equal(module.liveSelector()(h.state()), true); release(); await settle();
  console.log('PASS bounded connection heartbeat, upload suspension, silent-connection recovery');
}
{
  const h = harness(); h.open(); const module = h.client.module('probe');
  const release1 = module.useState(), release2 = module.useState(null);
  assert.equal(h.sent.length, 1);
  h.ack(h.latest('state-subscribe'), { value: 1 });
  assert.equal(module.stateSelector(null)(h.state()).value, 1);
  assert.equal(module.stateSelector()(h.state()).value, 1);
  release1(); assert.equal(h.sent.length, 1); release2(); await settle();
  assert.equal(h.sent.filter(request => request.name === 'state-unsubscribe').length, 1);
  console.log('PASS omitted/null queries share the default stream and cache');
}
{
  for (const code of ['unsupported-state', 'invalid-subscription', 'subscription-conflict']) {
    const h = harness(); h.open(); const module = h.client.module('probe');
    const release = module.useState(); const rejected = h.latest('state-subscribe');
    h.response(rejected, undefined, { error: [code] }); await settle(); await settle();
    assert.equal(module.errorSelector()(h.state()), code);
    assert.equal(module.liveSelector()(h.state()), false);
    await h.advance(120_000);
    assert.equal(h.sent.length, 1, 'permanent rejection must not retry or unsubscribe an unregistered ID');
    const refreshing = h.client.refreshState('probe');
    const retry = h.latest('state-subscribe'); assert.notEqual(retry.data.id, rejected.data.id);
    h.ack(retry, { value: 1 }); assert.equal((await refreshing).data.value, 1);
    assert.equal(module.errorSelector()(h.state()), undefined);
    release(); await settle();
  }
  console.log('PASS terminal errors surface unavailable, stay quiet and permit explicit retry');
}
{
  for (const code of ['subscription-limit', 'snapshot-limit', 'snapshot-error']) {
    const h = harness(); h.open(); const release = h.client.module('probe').useState();
    h.response(h.latest('state-subscribe'), undefined, { error: [code] }); await settle(); await settle();
    assert.equal(h.sent.filter(request => request.name === 'state-subscribe').length, 1);
    await h.advance(999); assert.equal(h.sent.filter(request => request.name === 'state-subscribe').length, 1);
    await h.advance(1); assert.equal(h.sent.filter(request => request.name === 'state-subscribe').length, 2);
    h.response(h.latest('state-subscribe'), undefined, { error: [code] }); await settle(); await settle();
    await h.advance(1999); assert.equal(h.sent.filter(request => request.name === 'state-subscribe').length, 2);
    await h.advance(1); assert.equal(h.sent.filter(request => request.name === 'state-subscribe').length, 3);
    h.ack(h.latest('state-subscribe'), { value: 1 }); release(); await settle();
  }
  const timeout = harness(); timeout.open(); const release = timeout.client.module('probe').useState();
  await timeout.advance(10_000); await timeout.advance(999);
  assert.equal(timeout.sent.filter(request => request.name === 'state-subscribe').length, 1);
  await timeout.advance(1); assert.equal(timeout.sent.filter(request => request.name === 'state-subscribe').length, 2);
  timeout.ack(timeout.latest('state-subscribe'), { value: 1 }); release(); await settle();
  console.log('PASS transient limits/errors/timeouts use bounded retry backoff');
}
{
  const h = harness(); const { ServerClock, sampleAge } = h.load('clock');
  const clock = new ServerClock(); let notified = 0;
  const first = clock.subscribe(() => ++notified);
  assert.equal(clock.getSnapshot(), undefined); assert.equal(h.timerCount(), 0);
  clock.observe(0); assert.equal(h.timerCount(), 1);
  const second = clock.subscribe(() => ++notified); assert.equal(h.timerCount(), 1);
  await h.advance(2000); assert.equal(clock.getSnapshot(), 2000); assert(notified >= 3);
  first(); assert.equal(h.timerCount(), 1); second(); assert.equal(h.timerCount(), 0);
  clock.reset(); assert.equal(clock.getSnapshot(), undefined);
  assert.equal(sampleAge(0, 100), 100); assert.equal(sampleAge(-1, 100), -1);
  assert.equal(sampleAge(null, 100), -1); assert.equal(sampleAge(101, 100), -1);
  assert.equal(sampleAge(0, undefined), -1);
  console.log('PASS production monotonic clock, timestamp0, invalid/future stamps and shared timer lifetime');
}
{
  const h = harness(); h.open(); const release = h.client.module('probe').useState();
  h.ack(h.latest('state-subscribe'), { sampledAtMs: 900, value: 1 });
  assert.equal(h.renderAge(900), 100); const count = h.sent.length;
  await h.advance(2000); assert.equal(h.renderAge(900), 2100);
  assert.equal(h.sent.length, count, 'local age ticks send no WS requests');
  h.event('probe', { stream: 'stream-probe', baseRev: 1, rev: 2,
    patch: [{ op: 'set', path: ['sampledAtMs'], value: 3000 }] });
  assert.equal(h.state().backend.modules.probe.state.value, 1);
  assert.equal(h.renderAge(3000), 0, 'a successful equal-value sample renews exact acquisition time');
  h.close(); assert.equal(h.renderAge(3000), -1);
  h.open();
  h.receive({ type: 'response', source: 'ui', name: 'ping', data: { serverTimeMs: 20 } });
  assert.equal(h.renderAge(3000), -1, 'cached oldboot state cannot use a new connection clock');
  const request = h.latest('state-subscribe');
  h.response(request, { id: request.data.id, source: 'probe', stream: 'stream-probe', rev: 1,
    state: { sampledAtMs: 10, value: 1 }, serverTimeMs: 30 });
  assert.equal(h.renderAge(10), 20); release(); h.unmount(); await settle();
  console.log('PASS actual age hook, unchanged values/new sample, local ticking, disconnect and reboot');
}
{
  const h = harness(); h.open(); const release = h.client.module('probe').useState();
  const request = h.latest('state-subscribe');
  h.response(request, { id: request.data.id, source: 'probe', stream: 'stream-probe', rev: 1,
    state: { sampledAtMs: 0 }, serverTimeMs: 100000 });
  assert.equal(h.renderAge(0), 100000, 'opening a view does not make its old sample fresh');
  release(); h.unmount(); await settle();
  const once = harness(); once.open(); const pending = once.client.getState('probe'); await once.advance(100);
  once.response(once.latest('state-get'), { sampledAtMs: 4000 }, { serverTimeMs: 5000 }); await pending;
  assert.equal(once.renderAge(4000, 'probe', { once: true }), 1000);
  assert.equal(once.renderAge(4000), -1, 'an unobserved inactive view does not borrow another clock');
  once.close(); assert.equal(once.renderAge(4000, 'probe', { once: true }), -1); once.unmount();
  console.log('PASS old initial snapshot age and one-shot view connection provenance');
}
console.log('All WebSocket subscription UI tests passed.');
