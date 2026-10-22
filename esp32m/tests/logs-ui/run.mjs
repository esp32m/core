// Execute production TS/TSX with deterministic React hooks and transport doubles.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import path from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const core = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "../..",
);
const require = createRequire(path.join(core, "web-ui/package.json"));
const babel = require("@babel/core");
const directory = path.join(core, "web-ui/esp32m/ui/src/system/logs");
const compiled = new Map();
for (const file of [
  "api.ts",
  "types.ts",
  "useLogs.ts",
  "BrowserDialog.tsx",
  "SettingsDialog.tsx",
  "Logs.tsx",
]) {
  compiled.set(
    file.replace(/\.tsx?$/, ""),
    (
      await babel.transformAsync(
        readFileSync(path.join(directory, file), "utf8"),
        {
          filename: file,
          configFile: false,
          babelrc: false,
          presets: [
            [
              require.resolve("@babel/preset-typescript"),
              { onlyRemoveTypeImports: false },
            ],
            [
              require.resolve("@babel/preset-react"),
              { runtime: "automatic", development: false },
            ],
          ],
          plugins: [
            require.resolve("@babel/plugin-transform-modules-commonjs"),
          ],
        },
      )
    ).code,
  );
}
for (const file of ["hooks", "subscriptions", "clock"]) {
  const filename = path.join(
    core,
    "web-ui/esp32m/ui/src/backend",
    `${file}.ts`,
  );
  compiled.set(
    `backend/${file}`,
    (
      await babel.transformAsync(readFileSync(filename, "utf8"), {
        filename,
        configFile: false,
        babelrc: false,
        presets: [
          [
            require.resolve("@babel/preset-typescript"),
            { onlyRemoveTypeImports: false },
          ],
        ],
        plugins: [require.resolve("@babel/plugin-transform-modules-commonjs")],
      })
    ).code,
  );
}

const info = {
  schema: 1,
  supportsQuotaPercent: true,
  quotaBaseBytes: 1048576,
  available: true,
  filesystem: "littlefs",
  label: "storage",
  totalBytes: 1048576,
  freeBytes: 800000,
  reserveBytes: 65536,
  minQuotaBytes: 8192,
  maxQuotaBytes: 983040,
  pageBytes: 4096,
  maxPageBytes: 8192,
};
const config = { enabled: false, quotaBytes: 262144, maxAgeDays: 7 };
const state = {
  available: true,
  enabled: false,
  ready: true,
  usedBytes: 10000,
  quotaBytes: 262144,
  effectiveQuotaBytes: 262144,
  freeBytes: 800000,
  reserveBytes: 65536,
  fileCount: 2,
  dropped: 0,
  truncated: 0,
  pendingBytes: 0,
  timeValid: false,
  lastError: "",
};
const files = [
  {
    id: "old",
    size: 7,
    active: false,
    bootId: "boot1",
    firstTime: null,
    lastTime: null,
  },
  {
    id: "new & current",
    size: 10000,
    active: true,
    bootId: "boot2",
    firstTime: Date.UTC(2026, 9, 21, 10, 0),
    lastTime: Date.UTC(2026, 9, 21, 10, 1),
  },
];
const content = "x".repeat(9900) + '<script>alert("log text")</script>\n';
const clone = (value) => JSON.parse(JSON.stringify(value));
const tick = () => new Promise((resolve) => setImmediate(resolve));
const deferred = () => {
  let resolve;
  const promise = new Promise((done) => {
    resolve = done;
  });
  return { promise, resolve };
};

function transport(handler) {
  const calls = [],
    listeners = new Set();
  const stateListeners = new Set();
  const subscriptions = [];
  let watched = 0;
  let connection = "connected";
  let snapshot = { state: clone(state), live: false, error: undefined };
  const notify = () => {
    for (const listener of stateListeners) listener();
  };
  const module = {
    stateSelector: () => (value) => value.state,
    liveSelector: () => (value) => value.live,
    errorSelector: () => (value) => value.error,
    useState() {
      ++watched;
      subscriptions.push("subscribe");
      snapshot = {
        ...snapshot,
        live: connection === "connected" && api.autoSnapshot,
        error: undefined,
      };
      notify();
      return () => {
        --watched;
        subscriptions.push("unsubscribe");
        snapshot = { ...snapshot, live: false };
        notify();
      };
    },
  };
  const api = {
    calls,
    subscriptions,
    autoSnapshot: true,
    get watched() {
      return watched;
    },
    get snapshot() {
      return snapshot;
    },
    module: () => module,
    observeState(callback) {
      stateListeners.add(callback);
      return () => stateListeners.delete(callback);
    },
    pushState(value, live = true, error) {
      snapshot = {
        state: clone(value),
        live: live && connection === "connected",
        error,
      };
      notify();
    },
    status: {
      is: (value) => value === connection,
      changed: {
        subscribe(callback) {
          listeners.add(callback);
          return { unsubscribe: () => listeners.delete(callback) };
        },
      },
      set(value) {
        connection = value;
        snapshot = { ...snapshot, live: false };
        notify();
        for (const listener of listeners) listener();
      },
    },
    async request(target, name, data) {
      calls.push({ target, name, data });
      const result = handler ? await handler(target, name, data) : undefined;
      if (result !== undefined) return { data: result };
      if (target === "app" && name === "describe")
        return { data: { logs: { schema: 1 } } };
      if (name === "info-get") return { data: clone(info) };
      if (name === "state-get") return { data: clone(state) };
      if (name === "config-get") return { data: clone(config) };
      if (name === "config-set") return { data };
      if (name === "list")
        return { data: { generation: "g1", files: clone(files) } };
      if (name === "read") {
        const text = data.id === "old" ? "old log" : content;
        const bytes = Buffer.from(text);
        const offset = data.tail
          ? Math.max(0, bytes.length - data.limit)
          : Math.min(bytes.length, data.offset || 0);
        const end = Math.min(offset + data.limit, bytes.length);
        return {
          data: {
            id: data.id,
            generation: "g1",
            offset,
            end,
            size: bytes.length,
            text: bytes.subarray(offset, end).toString(),
            eof: end === bytes.length,
          },
        };
      }
      throw new Error(`Unexpected ${target}/${name}`);
    },
    getInfo(target) {
      return api.request(target, "info-get");
    },
    getState(target) {
      return api.request(target, "state-get");
    },
    getConfig(target) {
      return api.request(target, "config-get");
    },
    setConfig(target, data) {
      return api.request(target, "config-set", data);
    },
  };
  return api;
}

function harness(file, exported, api, props = {}) {
  const hooks = [],
    effects = [],
    modules = new Map();
  let cursor = 0,
    dirty = true,
    tree,
    mounted = true,
    afterUnmount = 0;
  const same = (a, b) =>
    a && b && a.length === b.length && a.every((v, i) => Object.is(v, b[i]));
  const slot = (create) => {
    const i = cursor++;
    if (!(i in hooks)) hooks[i] = create();
    return i;
  };
  const react = {
    createContext: (value) => ({ value }),
    useContext: () => api,
    useState(initial) {
      const i = slot(() =>
        typeof initial === "function" ? initial() : initial,
      );
      return [
        hooks[i],
        (value) => {
          if (!mounted) {
            ++afterUnmount;
            return;
          }
          const next = typeof value === "function" ? value(hooks[i]) : value;
          if (!Object.is(next, hooks[i])) {
            hooks[i] = next;
            dirty = true;
          }
        },
      ];
    },
    useRef(initial) {
      return hooks[slot(() => ({ current: initial }))];
    },
    useCallback(callback, deps) {
      const i = slot(() => ({}));
      if (!same(hooks[i].deps, deps)) hooks[i] = { callback, deps };
      return hooks[i].callback;
    },
    useMemo(factory, deps) {
      const i = slot(() => ({}));
      if (!same(hooks[i].deps, deps)) hooks[i] = { value: factory(), deps };
      return hooks[i].value;
    },
    useEffect(callback, deps) {
      const i = slot(() => ({}));
      if (!same(hooks[i].deps, deps)) {
        const previous = hooks[i];
        hooks[i] = { deps, cleanup: previous.cleanup };
        effects.push(() => {
          previous.cleanup?.();
          hooks[i].cleanup = callback();
        });
      }
    },
  };
  const jsx = (type, props) => ({ type, props: props || {} });
  const context = {
    console,
    URL,
    TextEncoder,
    window: {
      esp32m: { backend: { host: "device.test:8080" } },
      location: { origin: "http://ui.test" },
    },
  };
  function load(name) {
    if (modules.has(name)) return modules.get(name);
    const exports = {};
    modules.set(name, exports);
    vm.runInNewContext(
      `(function(exports, require){${compiled.get(name)}\n})`,
      context,
    )(exports, (id) => {
      if (id === "react") return react;
      if (id === "react/jsx-runtime")
        return { jsx, jsxs: jsx, Fragment: "Fragment" };
      if (id === "@mui/material")
        return new Proxy({}, { get: (_, name) => name });
      if (id === "@ts-libs/ui-app") return { CardBox: "CardBox" };
      if (id === "@ts-libs/ui-i18n")
        return { useTranslation: () => ({ t: (key) => key }) };
      if (id === "@ts-libs/tools")
        return {
          errorToString: (error) => error?.message ?? String(error),
          isNumber: (value) => typeof value === "number",
          noop() {},
        };
      if (id === "react-redux")
        return {
          useSelector(selector) {
            const [, update] = react.useState(0);
            react.useEffect(
              () => api.observeState(() => update((value) => value + 1)),
              [api],
            );
            return selector(api.snapshot);
          },
          useDispatch: () => () => {},
        };
      if (id === "@ts-libs/ui-snack")
        return { useSnackApi: () => ({ error() {} }) };
      if (name === "backend/hooks" && id === "./state")
        return { Name: "backend" };
      if (name.startsWith("backend/") && id.startsWith("./"))
        return load(`backend/${id.slice(2)}`);
      if (id === "../../backend")
        return {
          useBackendApi: () => api,
          useSubscribedState: load("backend/hooks").useSubscribedState,
          ConnectionStatus: { Connected: "connected" },
        };
      if (id === "../../utils") return { formatBytes: (value) => `${value} B` };
      if (id === "../../app") return { NameValueList: "NameValueList" };
      if (id.startsWith("./")) return load(id.slice(2));
      throw new Error(`Unexpected import ${id}`);
    });
    return exports;
  }
  const component = load(file)[exported];
  const render = () => {
    cursor = 0;
    dirty = false;
    tree = component(props);
    for (const effect of effects.splice(0)) effect();
    return tree;
  };
  const flush = async () => {
    for (let i = 0; i < 8; ++i) {
      if (dirty) render();
      await tick();
    }
    if (dirty) render();
    return tree;
  };
  return {
    load,
    render,
    flush,
    get tree() {
      return tree;
    },
    get afterUnmount() {
      return afterUnmount;
    },
    unmount() {
      for (const hook of hooks) hook?.cleanup?.();
      mounted = false;
      dirty = false;
    },
  };
}

function nodes(node, type) {
  if (!node || typeof node !== "object") return [];
  if (Array.isArray(node)) return node.flatMap((child) => nodes(child, type));
  return [
    ...(node.type === type ? [node] : []),
    ...nodes(node.props?.children, type),
    ...nodes(node.props?.control, type),
  ];
}
function text(node) {
  if (Array.isArray(node)) return node.map(text).join("");
  if (!node || typeof node === "boolean") return "";
  return typeof node === "object" ? text(node.props?.children) : String(node);
}
function button(test, label) {
  const found = nodes(test.tree, "Button").find((node) => text(node) === label);
  assert(found, `Missing ${label}`);
  return found.props;
}
function field(test, label) {
  const found = nodes(test.tree, "TextField").find(
    (node) => node.props.label === label,
  );
  assert(found, `Missing ${label}`);
  return found.props;
}

// Missing/older modules are detected without an unanswered logs request.
{
  const api = transport((target, name) =>
    target === "app" && name === "describe" ? {} : undefined,
  );
  const test = harness("useLogs", "useLogs", api);
  await test.flush();
  assert.equal(test.tree.supported, false);
  assert.deepEqual(
    api.calls.map((call) => call.target),
    ["app"],
  );
  await test.flush();
  assert.equal(api.calls.length, 1, "No background status/history polling");
  assert.equal(
    api.watched,
    0,
    "Unsupported modules never register a state interest",
  );
  test.unmount();
}

// Reconnect reloads metadata and obtains a fresh shared subscription, never history. An obsolete
// response from a previous connection cannot overwrite the new capability.
{
  const pending = deferred();
  let descriptions = 0;
  const api = transport((target, name) => {
    if (target === "app" && name === "describe" && ++descriptions === 1)
      return pending.promise;
  });
  const test = harness("useLogs", "useLogs", api);
  await test.flush();
  api.status.set("connecting");
  await test.flush();
  api.status.set("connected");
  await test.flush();
  assert.equal(test.tree.supported, true);
  assert.equal(test.tree.config.enabled, false);
  assert.equal(api.watched, 1);
  pending.resolve({});
  await test.flush();
  assert.equal(test.tree.supported, true);
  assert(!api.calls.some((call) => ["list", "read"].includes(call.name)));
  assert(
    !api.calls.some((call) => call.name === "state-get"),
    "No private snapshot reads",
  );
  test.unmount();
  assert.equal(api.watched, 0);
  assert.deepEqual(api.subscriptions, ["subscribe", "unsubscribe"]);
}

// The card makes unavailable firmware read-only and surfaces storage loss.
{
  const absent = harness(
    "Logs",
    "Logs",
    transport((target, name) =>
      target === "app" && name === "describe" ? {} : undefined,
    ),
  );
  await absent.flush();
  assert(
    text(absent.tree).includes("This firmware does not support stored logs."),
  );
  assert.equal(button(absent, "Browse").disabled, true);
  absent.unmount();
  const api = transport();
  const test = harness("Logs", "Logs", api);
  await test.flush();
  api.pushState({ ...clone(state), dropped: 2, lastError: "Storage full" });
  await test.flush();
  assert(text(test.tree).includes("Storage full"));
  assert(text(test.tree).includes("Stored history is incomplete."));
  assert(text(test.tree).includes("Time is not synchronized."));
  assert(!nodes(test.tree, "Button").some((node) => text(node) === "Refresh"));
  button(test, "Settings").onClick();
  await test.flush();
  assert(!api.calls.some((call) => call.name === "config-set"));
  api.status.set("connecting");
  await test.flush();
  assert.equal(button(test, "Browse").disabled, true);
  test.unmount();
}

// Shared-hook pushes update the card immediately, including a remote client's
// policy/availability changes. Metadata reload does not recreate its interest.
{
  const api = transport();
  const test = harness("Logs", "Logs", api);
  await test.flush();
  const before = api.calls.length;
  api.pushState({
    ...state,
    enabled: true,
    ready: true,
    quotaPercent: 75,
    quotaBytes: 600000,
    usedBytes: 12345,
    freeBytes: 500000,
    reserveBytes: 16384,
    quotaBaseBytes: 512345,
    dropped: 4,
  });
  await test.flush();
  const rows = nodes(test.tree, "NameValueList")[0].props.list;
  assert.equal(rows.find((row) => row[0] === "Capture")[1], "Recording");
  assert.equal(
    rows.find((row) => row[0] === "Quota policy")[1],
    "75% of free space",
  );
  assert.equal(
    rows.find((row) => row[0] === "Stored / quota")[1],
    "12345 B / 600000 B",
  );
  assert.equal(
    api.calls.length,
    before,
    "Pushes do not fetch info/config/history",
  );
  api.pushState({ ...state, quotaPercent: 0, available: false });
  await test.flush();
  assert.equal(button(test, "Browse").disabled, true);
  assert(
    text(test.tree).includes("Log storage is unavailable on this device."),
  );
  assert(
    !nodes(test.tree, "NameValueList")[0].props.list.some(
      (row) => row[0] === "Quota policy",
    ),
  );
  api.pushState({ ...state, available: true });
  await test.flush();
  assert.equal(button(test, "Browse").disabled, false);
  button(test, "Settings").onClick();
  await test.flush();
  assert.equal(api.watched, 1);
  assert.deepEqual(api.subscriptions, ["subscribe"]);
  assert(
    !api.calls.some((call) =>
      ["state-get", "list", "read"].includes(call.name),
    ),
  );
  test.unmount();
  assert.equal(api.watched, 0);
  api.pushState({ ...state, usedBytes: 999999 });
  await tick();
  assert.equal(test.afterUnmount, 0);
}

// Reconnect retains no actionable old state while waiting for the coordinator's
// new snapshot. Subscription failures are visible and disable storage actions.
{
  const api = transport();
  const test = harness("useLogs", "useLogs", api);
  await test.flush();
  assert(test.tree.live);
  api.status.set("connecting");
  await test.flush();
  assert.equal(test.tree.state, undefined);
  assert.equal(api.watched, 0);
  api.autoSnapshot = false;
  api.status.set("connected");
  await test.flush();
  assert.equal(api.watched, 1);
  assert.equal(test.tree.live, false);
  assert.equal(test.tree.state, undefined);
  api.pushState({ ...state, usedBytes: 42 });
  await test.flush();
  assert.equal(test.tree.state.usedBytes, 42);
  api.pushState({ ...state, usedBytes: 999 }, false, "unsupported-state");
  await test.flush();
  assert.equal(test.tree.state, undefined);
  assert.equal(test.tree.error, "unsupported-state");
  assert.equal(test.tree.loading, false);
  assert(!api.calls.some((call) => call.name === "state-get"));
  test.unmount();
  assert.deepEqual(api.subscriptions, [
    "subscribe",
    "unsubscribe",
    "subscribe",
    "unsubscribe",
  ]);
}

// Paged ascending metadata still opens the active/newest segment, read paging
// uses byte offsets, text remains escaped React content, and export is native.
{
  const api = transport((target, name, data) => {
    if (name === "list")
      return data.offset === 0
        ? { generation: "g1", files: [clone(files[0])], nextOffset: 1 }
        : { generation: "g1", files: [clone(files[1])] };
  });
  const test = harness("BrowserDialog", "BrowserDialog", api, {
    info,
    onClose() {},
  });
  await test.flush();
  assert.equal(field(test, "Log file").value, files[1].id);
  assert(
    text(test.tree).includes(new Date(files[1].firstTime).toLocaleString()),
    "File timestamps are Unix milliseconds",
  );
  let read = api.calls.filter((call) => call.name === "read").at(-1);
  assert.equal(read.data.tail, true);
  assert.equal(read.data.limit, 4096);
  const pre = nodes(test.tree, "Box").find(
    (node) => node.props.component === "pre",
  );
  assert(text(pre).includes("<script>"), "Logs must remain literal text");
  assert.equal(pre.props.dangerouslySetInnerHTML, undefined);
  assert.equal(
    new URL(button(test, "Export file").href).searchParams.get("id"),
    files[1].id,
  );
  assert.equal(
    new URL(button(test, "Export all logs").href).host,
    "device.test:8080",
  );
  const tailStart = Buffer.byteLength(content) - 4096;
  await button(test, "Older").onClick();
  await test.flush();
  read = api.calls.filter((call) => call.name === "read").at(-1);
  assert.equal(read.data.offset, Math.max(0, tailStart - 4096));
  await button(test, "Newer").onClick();
  await test.flush();
  read = api.calls.filter((call) => call.name === "read").at(-1);
  assert.equal(read.data.offset, tailStart);
  const before = api.calls.length;
  await test.flush();
  assert.equal(api.calls.length, before, "Viewer never polls history");
  test.unmount();
}

// Empty stores succeed; non-progressing catalog pages cannot loop forever.
{
  const empty = harness(
    "BrowserDialog",
    "BrowserDialog",
    transport((target, name) =>
      name === "list" ? { generation: "g1", files: [] } : undefined,
    ),
    { info, onClose() {} },
  );
  await empty.flush();
  assert(text(empty.tree).includes("No stored logs."));
  assert.equal(button(empty, "Export all logs").disabled, true);
  empty.unmount();
  const api = transport((target, name) =>
    name === "list"
      ? { generation: "g1", files: [], nextOffset: 1 }
      : undefined,
  );
  const test = harness("BrowserDialog", "BrowserDialog", api, {
    info,
    onClose() {},
  });
  await test.flush();
  assert(text(test.tree).includes("browser limit"));
  assert.equal(api.calls.length, 1);
  test.unmount();
}

// Closing during an outstanding list stops the subsequent read and all state
// updates; stale/busy read failures remain visible and never retry automatically.
{
  const pending = deferred();
  const api = transport((target, name) =>
    name === "list" ? pending.promise : undefined,
  );
  const test = harness("BrowserDialog", "BrowserDialog", api, {
    info,
    onClose() {},
  });
  await test.flush();
  test.unmount();
  pending.resolve({ generation: "g1", files: clone(files) });
  await test.flush();
  assert(!api.calls.some((call) => call.name === "read"));
  assert.equal(test.afterUnmount, 0);
}
{
  const api = transport((target, name) => {
    if (name === "read") throw new Error("Log was pruned");
  });
  const test = harness("BrowserDialog", "BrowserDialog", api, {
    info,
    onClose() {},
  });
  await test.flush();
  assert(text(test.tree).includes("Log was pruned"));
  assert.equal(api.calls.filter((call) => call.name === "read").length, 1);
  test.unmount();
}

// Settings edits and Cancel never write. Unit changes preserve the byte quota;
// invalid values cannot save, trimming is explicit, and duplicate OK is guarded.
{
  let cancelled = 0,
    saved = 0;
  const pending = deferred();
  const api = transport((target, name) =>
    name === "config-set" ? pending.promise : undefined,
  );
  const test = harness("SettingsDialog", "SettingsDialog", api, {
    info,
    config,
    onClose() {
      ++cancelled;
    },
    onSaved() {
      ++saved;
    },
  });
  await test.flush();
  field(test, "Storage quota").onChange({ target: { value: "128" } });
  await test.flush();
  assert(text(test.tree).includes("may permanently remove older logs"));
  button(test, "Cancel").onClick();
  assert.equal(cancelled, 1);
  assert.equal(api.calls.length, 0);
  field(test, "Unit").onChange({ target: { value: "1048576" } });
  await test.flush();
  assert.equal(field(test, "Storage quota").value, "0.125");
  field(test, "Maximum age (days)").onChange({ target: { value: "-1" } });
  await test.flush();
  assert.equal(button(test, "OK").disabled, true);
  field(test, "Maximum age (days)").onChange({ target: { value: "0" } });
  await test.flush();
  const ok = button(test, "OK");
  const save = ok.onClick();
  ok.onClick();
  await test.flush();
  assert.equal(api.calls.length, 1);
  assert.deepEqual(clone(api.calls[0].data), {
    enabled: false,
    quotaBytes: 131072,
    quotaPercent: 0,
    maxAgeDays: 0,
  });
  assert.equal(button(test, "Cancel").disabled, true);
  pending.resolve({});
  await save;
  await test.flush();
  assert.equal(saved, 1);
  test.unmount();
}

// Rejected settings stay open with their edits and an explicit error.
{
  let saved = false;
  const api = transport((target, name) => {
    if (name === "config-set") throw new Error("No viable quota");
  });
  const test = harness("SettingsDialog", "SettingsDialog", api, {
    info,
    config,
    onClose() {},
    onSaved() {
      saved = true;
    },
  });
  await test.flush();
  field(test, "Storage quota").onChange({ target: { value: "128" } });
  await test.flush();
  await button(test, "OK").onClick();
  await test.flush();
  assert.equal(saved, false);
  assert(text(test.tree).includes("No viable quota"));
  assert.equal(field(test, "Storage quota").value, "128");
  assert.equal(button(test, "OK").disabled, false);
  test.unmount();
}

// Percentage mode opens at the saved default 50%, converts using the common
// own-log-excluding base, and remains authoritative after save/reopen.
{
  let saved;
  const percentConfig = { ...config, quotaPercent: 50, quotaBytes: 524288 };
  const api = transport((target, name, data) => {
    if (name === "config-set") {
      saved = clone(data);
      return data;
    }
  });
  const test = harness("SettingsDialog", "SettingsDialog", api, {
    info,
    config: percentConfig,
    onClose() {},
    onSaved() {},
  });
  await test.flush();
  assert.equal(field(test, "Unit").value, 0);
  assert.equal(field(test, "Storage quota").value, "50");
  assert(text(test.tree).includes("Calculated quota"));
  field(test, "Unit").onChange({ target: { value: "1024" } });
  await test.flush();
  assert.equal(field(test, "Storage quota").value, "512");
  field(test, "Unit").onChange({ target: { value: "1048576" } });
  await test.flush();
  assert.equal(field(test, "Storage quota").value, "0.5");
  field(test, "Unit").onChange({ target: { value: "0" } });
  await test.flush();
  assert.equal(field(test, "Storage quota").value, "50");
  field(test, "Storage quota").onChange({ target: { value: "25" } });
  await test.flush();
  assert(text(test.tree).includes("may permanently remove older logs"));
  await button(test, "OK").onClick();
  await test.flush();
  assert.deepEqual(saved, {
    enabled: false,
    quotaBytes: 262144,
    quotaPercent: 25,
    maxAgeDays: 7,
  });
  test.unmount();
  const reopened = harness("SettingsDialog", "SettingsDialog", api, {
    info,
    config: saved,
    onClose() {},
    onSaved() {},
  });
  await reopened.flush();
  assert.equal(field(reopened, "Unit").value, 0);
  assert.equal(field(reopened, "Storage quota").value, "25");
  reopened.unmount();
}

// Cancel preserves the existing percentage. Old backends without the capability
// cannot show or save percentage mode; legacy byte settings retain their units.
{
  const api = transport();
  const initial = { ...config, quotaPercent: 50, quotaBytes: 524288 };
  const test = harness("SettingsDialog", "SettingsDialog", api, {
    info,
    config: initial,
    onClose() {},
    onSaved() {},
  });
  await test.flush();
  field(test, "Storage quota").onChange({ target: { value: "30" } });
  await test.flush();
  button(test, "Cancel").onClick();
  test.unmount();
  assert.equal(api.calls.length, 0);
  const reopened = harness("SettingsDialog", "SettingsDialog", api, {
    info,
    config: initial,
    onClose() {},
    onSaved() {},
  });
  await reopened.flush();
  assert.equal(field(reopened, "Storage quota").value, "50");
  reopened.unmount();
  const legacyInfo = { ...info };
  delete legacyInfo.supportsQuotaPercent;
  delete legacyInfo.quotaBaseBytes;
  const legacy = harness("SettingsDialog", "SettingsDialog", api, {
    info: legacyInfo,
    config,
    onClose() {},
    onSaved() {},
  });
  await legacy.flush();
  assert.equal(field(legacy, "Unit").value, 1024);
  assert.equal(field(legacy, "Storage quota").value, "256");
  assert(
    !nodes(field(legacy, "Unit"), "MenuItem").some(
      (node) => node.props.value === 0,
    ),
  );
  assert(!text(legacy.tree).includes("% of free space"));
  legacy.unmount();
}

// Percentage bounds, reserve pressure, baseline stability and rounding.
{
  const test = harness("SettingsDialog", "SettingsDialog", transport(), {
    info,
    config: { ...config, quotaPercent: 50 },
    onClose() {},
    onSaved() {},
  });
  await test.flush();
  const helpers = test.load("api");
  for (const value of ["0", "-1", "101", "50.5", "", "Infinity"]) {
    assert.equal(helpers.settingsConfig(false, value, 0, "7", info), null);
    field(test, "Storage quota").onChange({ target: { value } });
    await test.flush();
    assert.equal(button(test, "OK").disabled, true);
  }
  assert.equal(
    helpers.settingsConfig(false, "1", 0, "7", info).quotaBytes,
    10485,
  );
  assert.equal(
    helpers.settingsConfig(false, "100", 0, "7", info).quotaBytes,
    1048576,
  );
  assert.equal(
    helpers.quotaPreview("100", 0, info).effectiveQuotaBytes,
    983040,
  );
  const legacy = { ...info };
  delete legacy.supportsQuotaPercent;
  delete legacy.quotaBaseBytes;
  assert.equal(helpers.settingsConfig(false, "50", 0, "7", legacy), null);
  assert.equal(helpers.quotaBaseBytes(legacy), legacy.freeBytes);
  assert.equal(
    helpers.quotaBaseBytes(legacy, {
      ...state,
      freeBytes: 500000,
      usedBytes: 300000,
    }),
    800000,
  );
  assert.equal(
    helpers.quotaBaseBytes(legacy, {
      ...state,
      freeBytes: 400000,
      usedBytes: 400000,
    }),
    800000,
    "Own log growth must not shrink the baseline",
  );
  assert.equal(helpers.convertQuota("300", 1024, 0, 1048576), "29");
  assert(
    helpers.trimsHistory(
      { ...config, quotaPercent: 50, quotaBytes: 10000 },
      { ...config, quotaPercent: 25, quotaBytes: 20000 },
    ),
    "Percentage reduction warns even when other free space has increased",
  );
  test.unmount();
  const lowInfo = { ...info, quotaBaseBytes: 70000 };
  assert(
    helpers.settingsConfig(false, "8", 1024, "7", lowInfo),
    "A valid fixed-byte policy can also be disabled under reserve pressure",
  );
  assert.equal(helpers.settingsConfig(true, "8", 1024, "7", lowInfo), null);
  const low = harness("SettingsDialog", "SettingsDialog", transport(), {
    info: lowInfo,
    config: { ...config, quotaPercent: 50 },
    onClose() {},
    onSaved() {},
  });
  await low.flush();
  assert.equal(
    button(low, "OK").disabled,
    false,
    "A valid disabled percentage policy can be saved even with little space",
  );
  assert(text(low.tree).includes("Not enough space for the minimum log quota"));
  nodes(low.tree, "Switch")[0].props.onChange({}, true);
  await low.flush();
  assert.equal(
    button(low, "OK").disabled,
    true,
    "Capture cannot be enabled below the minimum effective quota",
  );
  low.unmount();
}

// Invalid/oversized responses are bounded, including UTF-8 bytes rather than
// JS character counts. URL construction preserves configured host and ID text.
{
  const test = harness("useLogs", "useLogs", transport());
  const helpers = test.load("api");
  assert.equal(
    helpers.downloadUrl("a&b/є", "192.0.2.1:81", "http://ui.test"),
    "http://192.0.2.1:81/logs/download?id=a%26b%2F%D1%94",
  );
  assert.equal(
    helpers.downloadUrl(undefined, "[::1]:8080", "https://ui.test"),
    "https://[::1]:8080/logs/download",
  );
  assert.equal(
    helpers.downloadUrl(undefined, "", "http://ui.test"),
    "http://ui.test/logs/download",
  );
  assert.equal(
    helpers.settingsConfig(false, "8", 1024, "3650", info).quotaBytes,
    8192,
  );
  for (const [quota, age] of [
    ["", "7"],
    ["Infinity", "0"],
    ["7", "1"],
    ["960.1", "1"],
    ["256", "3651"],
    ["256", "1.5"],
  ])
    assert.equal(helpers.settingsConfig(false, quota, 1024, age, info), null);
  const page = {
    id: "old",
    generation: "g1",
    offset: 0,
    end: 1,
    size: 1,
    text: "x",
    eof: true,
  };
  assert.throws(
    () => helpers.checkPage({ ...page, text: "є".repeat(5000) }, "old"),
    /Invalid/,
  );
  assert.throws(
    () => helpers.checkPage({ ...page, offset: -1 }, "old"),
    /Invalid/,
  );
  assert.throws(
    () => helpers.checkList({ generation: "g1", files: [], nextOffset: -1 }),
    /Invalid/,
  );
}

console.log("logs-ui: 16 scenario groups passed");
