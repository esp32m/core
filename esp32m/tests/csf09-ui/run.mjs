// Production TSX/plugin exercised with deterministic hooks and transport.
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
const directory = path.join(core, "web-ui/esp32m/ui/src/dev/csf09");
const compiled = new Map();
for (const file of [
  "Content.tsx",
  "index.ts",
  "types.ts",
  "registry.ts",
  "devices.tsx",
]) {
  const filename =
    file === "registry.ts"
      ? path.join(core, "web-ui/ts-libs/plugins/src/index.ts")
      : file === "devices.tsx"
        ? path.join(directory, "../Content.tsx")
        : path.join(directory, file);
  compiled.set(
    file,
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
          [
            require.resolve("@babel/preset-react"),
            { runtime: "automatic", development: false },
          ],
        ],
        plugins: [require.resolve("@babel/plugin-transform-modules-commonjs")],
      })
    ).code,
  );
}
const normalState = [999950, 1, 25.25, -10.25, false, 0];
const normalConfig = { address: 1, baud: 9600 };
function harness(props = {}) {
  let state = normalState,
    config = { ...normalConfig },
    stateError,
    cursor = 0;
  const hooks = [],
    calls = [],
    queries = [],
    deviceNames = [],
    handlers = new Map();
  const control = { disabled: false, known: true, busy: false };
  const jsx = (type, props) => ({ type, props: props || {} });
  const slot = (initialValue) => {
    const index = cursor++;
    if (!(index in hooks)) hooks[index] = initialValue();
    return index;
  };
  const react = {
    useState(initialValue) {
      const index = slot(() =>
        typeof initialValue === "function" ? initialValue() : initialValue,
      );
      return [
        hooks[index],
        (value) => {
          hooks[index] =
            typeof value === "function" ? value(hooks[index]) : value;
        },
      ];
    },
    useRef(initialValue) {
      return hooks[slot(() => ({ current: initialValue }))];
    },
  };
  const api = {
    async getState(name) {
      calls.push({ method: "getState", name });
      return { data: state };
    },
    async request(name, command, data, options) {
      calls.push({
        name,
        command,
        data: JSON.parse(JSON.stringify(data)),
        options,
      });
      if (handlers.has(command)) return handlers.get(command)(data);
      if (command === "config-get") return { data: config };
      if (command === "config-set") {
        assert.notEqual(
          "address" in data && "connectAddress" in data,
          true,
          "physical and local address operations must be separate",
        );
        config = {
          address: data.connectAddress ?? data.address ?? config.address,
          baud: 9600,
        };
        if (Array.isArray(state))
          state = [state[0], config.address, ...state.slice(2)];
        return { data: config };
      }
      throw new Error(`Unexpected command: ${command}`);
    },
  };
  const devices = { name: "devices" },
    modules = new Map();
  function load(file) {
    if (modules.has(file)) return modules.get(file);
    const exports = {};
    modules.set(file, exports);
    vm.runInNewContext(
      `(function(exports, require) {${compiled.get(file)}\n})`,
      {},
    )(exports, (id) => {
      if (id === "react") return react;
      if (id === "react/jsx-runtime") return { jsx, jsxs: jsx };
      if (id === "@mui/material" || id === "@mui/icons-material")
        return new Proxy({}, { get: (_, key) => key });
      if (id === "@ts-libs/ui-app") return { CardBox: "CardBox" };
      if (id === "@ts-libs/tools")
        return { errorToString: (error) => error?.message ?? String(error) };
      if (id === "../../app") return { NameValueList: "NameValueList" };
      if (id === "../..")
        return {
          useBackendApi: () => api,
          useSampleAge: stamp => typeof stamp === "number" && stamp >= 0 ? 1000000 - stamp : -1,
          useSubscribedState: (name) => {
            queries.push(name);
            return [state, async () => state, !stateError, stateError];
          },
        };
      if (id === "../DeviceControl")
        return {
          useDeviceControl: (name) => {
            deviceNames.push(name);
            return control;
          },
          DeviceDisabledAlert: "DeviceDisabledAlert",
          DeviceDisableSetting: "DeviceDisableSetting",
        };
      if (id === "../shared") return { Devices: devices };
      if (id === "./types") return load("types.ts");
      if (id === "./Content") return load("Content.tsx");
      throw new Error(`Unexpected dependency: ${id}`);
    });
    return exports;
  }
  const { Content } = load("Content.tsx");
  const { Csf09 } = load("index.ts");
  const render = () => {
    cursor = 0;
    return Content(props);
  };
  const all = () => {
    const nodes = [];
    const visit = (node) => {
      if (Array.isArray(node)) return node.forEach(visit);
      if (
        !node ||
        typeof node !== "object" ||
        (node.type === "Dialog" && !node.props.open)
      )
        return;
      nodes.push(node);
      visit(node.props.children);
      visit(node.props.action);
    };
    visit(render());
    return nodes;
  };
  const text = (node) => {
    if (Array.isArray(node)) return node.map(text).join("");
    if (node == null || typeof node === "boolean") return "";
    if (typeof node === "object") {
      if (node.type === "Dialog" && !node.props.open) return "";
      return text(
        node.type === "NameValueList" ? node.props.list : node.props?.children,
      );
    }
    return String(node);
  };
  const find = (type, predicate = () => true) => {
    const node = all().find(
      (node) => node.type === type && predicate(node.props, node),
    );
    assert(node, `Missing visible ${type}`);
    return node.props;
  };
  return {
    api,
    calls,
    queries,
    deviceNames,
    control,
    render,
    all,
    text,
    find,
    Csf09,
    Content,
    devices,
    setState(value) {
      state = value;
    },
    setStateError(value) {
      stateError = value;
    },
    setConfig(value) {
      config = value;
    },
    setProps(value) {
      props = value;
    },
    handler(command, callback) {
      handlers.set(command, callback);
    },
    clearHandler(command) {
      handlers.delete(command);
    },
    requests(command) {
      return calls.filter((call) => call.command === command);
    },
    field() {
      return find(
        "TextField",
        (field) => field.label === "Modbus slave address",
      );
    },
    connectField() {
      return find(
        "TextField",
        (field) => field.label === "Sensor ID to connect to",
      );
    },
    editConnect(value) {
      this.connectField().onChange({ target: { value: String(value) } });
    },
    edit(value) {
      this.field().onChange({ target: { value: String(value) } });
    },
    button(label) {
      return find("Button", (_, node) => text(node) === label);
    },
    row(label) {
      return find("NameValueList").list.find(([name]) => name === label)?.[1];
    },
    visibleText() {
      return all().map(text).join("\n");
    },
    open() {
      return find("IconButton").onClick();
    },
    dialogOpen() {
      return all().some((node) => node.type === "Dialog");
    },
  };
}
{
  const h = harness();
  for (const state of [
    undefined,
    null,
    {},
    [],
    [-1, 1, null, null],
    [939999, 1, 20, 10],
    [NaN, 1, 20, 10],
    [Infinity, 1, 20, 10],
    [null, 1, 20, 10],
    ["0", 1, 20, 10],
  ]) {
    h.setState(state);
    assert(h.render(), "settings remain reachable without a fresh sample");
    assert.equal(h.row("Moisture"), "Unavailable");
    assert.equal(h.row("Temperature"), "Unavailable");
    assert.match(h.visibleText(), /No recent sensor reading/);
  }
  for (const age of [0, 60000]) {
    h.setState([1000000 - age, 1, 20, 10]);
    assert.equal(h.row("Moisture"), "20.0 %");
  }
  assert.equal(
    h.calls.length,
    0,
    "ordinary renders do not read settings or write",
  );
}
{
  const h = harness();
  assert.equal(h.find("CardBox").title, "CSF09 soil sensor");
  assert.equal(h.row("Moisture"), "25.3 %");
  assert.equal(h.row("Temperature"), "-10.3 °C");
  assert.equal(h.row("MODBUS address"), 1);
  h.setState([1000000, 1, 0, 0]);
  assert.equal(h.row("Moisture"), "0.0 %");
  assert.equal(h.row("Temperature"), "0.0 °C");
  for (const bad of [null, undefined, NaN, Infinity, -Infinity, "20", {}]) {
    h.setState([999950, 1, bad, 10]);
    assert.equal(h.row("Moisture"), "Unavailable");
    assert.equal(h.row("Temperature"), "10.0 °C");
    h.setState([999950, 1, 20, bad]);
    assert.equal(h.row("Moisture"), "20.0 %");
    assert.equal(h.row("Temperature"), "Unavailable");
  }
  h.setState([999950, 1]);
  assert.equal(h.row("Moisture"), "Unavailable");
  assert.equal(h.row("Temperature"), "Unavailable");
  h.setProps({ addr: 2 });
  assert.equal(h.render(), null);
  h.setProps({ addr: 0 });
  assert.equal(h.render(), null, "zero is not a wildcard");
  h.setProps({ addr: 1 });
  assert(h.render());
  assert.equal(h.row("MODBUS address"), undefined);
  h.setState(undefined);
  assert(h.render(), "unknown address permits settings access");
  h.setProps({ name: "bed-one", title: "Garden bed" });
  assert.equal(h.find("CardBox").title, "Garden bed");
  assert.equal(h.queries.at(-1), "bed-one");
  assert.equal(h.deviceNames.at(-1), "bed-one");
  const props = { name: "bed-one", addr: 2, title: "Garden bed" },
    plugin = h.Csf09(props);
  assert.equal(plugin.name, "bed-one");
  assert.equal(h.Csf09().name, "CSF09");
  assert.equal(h.Csf09({ name: "" }).name, "CSF09");
  assert.equal(plugin.use, h.devices);
  assert.equal(plugin.device.component, h.Content);
  assert.equal(plugin.device.props, props);
}
{
  const h = harness({ name: "bed-one" });
  h.setState(undefined);
  await h.open();
  assert.equal(h.requests("config-get").length, 1);
  assert.equal(h.field().value, "1");
  assert.match(h.visibleText(), /Baud rate: 9600 \(8N1, fixed\)/);
  assert.equal(
    h.all().filter((node) => node.type === "TextField").length,
    2,
    "no baud editor",
  );
  assert(h.find("DeviceDisableSetting"));
  h.all();
  h.all();
  assert.equal(h.requests("config-get").length, 1);
  await h.button("Refresh").onClick();
  assert.equal(h.requests("config-get").length, 2);
  assert.equal(h.requests("config-set").length, 0);
  assert(h.calls.every((call) => call.name === "bed-one"));
  h.button("Cancel").onClick();
  assert.equal(h.dialogOpen(), false);
}
{
  const h = harness();
  await h.open();
  assert.equal(h.button("OK").disabled, true, "unchanged ID is a no-op");
  for (const invalid of [
    "",
    "0",
    "248",
    "1.5",
    "1e1",
    "-1",
    "NaN",
    " 2",
    "2 ",
  ]) {
    h.edit(invalid);
    assert.equal(h.button("OK").disabled, true, invalid);
    await h.button("OK").onClick();
  }
  h.edit(247);
  assert.equal(h.button("OK").disabled, false);
  assert.equal(h.requests("config-set").length, 0);
}
{
  const h = harness({ name: "bed-one", addr: 1 });
  await h.open();
  h.edit(2);
  let finish;
  h.handler("config-set", async (data) => {
    await new Promise((resolve) => {
      finish = resolve;
    });
    h.setConfig({ address: data.address, baud: 9600 });
    h.setState([1000000, data.address, 20, 10, false, 0]);
    return { data };
  });
  const save = h.button("OK"),
    first = save.onClick();
  await save.onClick();
  assert.equal(
    h.requests("config-set").length,
    1,
    "immediate duplicate OK sends once",
  );
  assert.equal(
    JSON.stringify(h.requests("config-set")[0].data),
    '{"address":2}',
  );
  assert.equal(h.requests("config-set")[0].options.timeout, 30000);
  assert.equal(h.field().disabled, true);
  for (const label of ["OK", "Connect", "Refresh", "Cancel"])
    assert.equal(h.button(label).disabled, true);
  h.find("Dialog").onClose();
  assert.equal(h.dialogOpen(), true, "active write cannot be dismissed");
  finish();
  await first;
  assert.equal(
    h.requests("config-get").length,
    2,
    "independent settings reload",
  );
  assert.equal(h.calls.filter((call) => call.method === "getState").length, 2);
  assert.equal(
    h.render(),
    null,
    "address filter applies once the dialog closes",
  );
  assert(h.calls.every((call) => call.name === "bed-one"));
}
{
  for (const config of [
    undefined,
    { address: 0, baud: 9600 },
    { address: 248, baud: 9600 },
    { address: "1", baud: 9600 },
    { address: 1, baud: 19200 },
    { ...normalConfig, communicationError: 263 },
    { ...normalConfig, pendingAddress: 2 },
  ]) {
    const h = harness();
    h.setConfig(config);
    await h.open();
    assert.equal(h.field().disabled, true, JSON.stringify(config));
    assert.equal(h.button("OK").disabled, true);
    assert.equal(h.requests("config-set").length, 0);
    assert.equal(h.button("Refresh").disabled, false);
  }
}
{
  const h = harness();
  await h.open();
  h.edit(2);
  for (const control of [
    { disabled: true },
    { busy: true },
    { known: false },
  ]) {
    Object.assign(
      h.control,
      { disabled: false, busy: false, known: true },
      control,
    );
    assert.equal(h.field().disabled, true);
    assert.equal(h.button("OK").disabled, true);
  }
  Object.assign(h.control, { disabled: false, busy: false, known: true });
  h.setState([1000000, 1, 20, 10, true, 0]);
  assert.equal(
    h.button("OK").disabled,
    true,
    "new pending state locks an open editor",
  );
  assert.equal(h.row("Moisture"), "Unavailable");
  assert.match(h.visibleText(), /awaiting verification/);
  assert.equal(h.requests("config-set").length, 0);
}
{
  const h = harness();
  await h.open();
  h.edit(2);
  h.handler("config-set", async () => {
    h.setConfig({ ...normalConfig, pendingAddress: 2, lastError: 263 });
    h.setState([1000000, 1, null, null, true, 263]);
    throw new Error("response lost");
  });
  await h.button("OK").onClick();
  assert.equal(h.requests("config-set").length, 1);
  assert.equal(h.requests("config-get").length, 2, "failed writes also reload");
  assert.equal(h.field().value, "1", "current ID replaces requested ID");
  assert.equal(h.button("OK").disabled, true);
  assert.match(h.visibleText(), /response lost/);
  assert.match(h.visibleText(), /do not repeat the write/);
  h.setConfig({ address: 2, baud: 9600 });
  h.setState([1000000, 2, 20, 10, false, 0]);
  await h.button("Refresh").onClick();
  assert.equal(h.field().value, "2");
  assert.equal(h.button("OK").disabled, true);
  assert.equal(
    h.requests("config-set").length,
    1,
    "refresh never repeats an uncertain write",
  );
}
{
  const h = harness();
  await h.open();
  h.edit(2);
  h.handler("config-set", async () => ({ data: {} }));
  await h.button("OK").onClick();
  assert.equal(h.dialogOpen(), true);
  assert.equal(h.field().value, "1");
  assert.match(h.visibleText(), /requested ID was not verified/);
  assert.equal(h.button("OK").disabled, true);
  assert.equal(h.requests("config-set").length, 1);
}
{
  const h = harness();
  await h.open();
  h.edit(2);
  h.handler("config-get", async () => {
    throw new Error("read timeout");
  });
  await h.button("OK").onClick();
  assert.equal(h.field().value, "");
  assert.equal(h.button("OK").disabled, true);
  assert.match(h.visibleText(), /Could not refresh settings: read timeout/);
  assert.equal(h.button("Refresh").disabled, false);
  h.clearHandler("config-get");
  await h.button("Refresh").onClick();
  assert.equal(h.field().value, "2");
  assert.equal(h.requests("config-set").length, 1);
}
{
  const h = harness();
  h.handler("config-get", async () => {
    throw new Error("offline");
  });
  await h.open();
  assert.equal(h.button("OK").disabled, true);
  assert.match(h.visibleText(), /offline/);
  h.clearHandler("config-get");
  await h.button("Refresh").onClick();
  h.edit(2);
  assert.equal(h.button("OK").disabled, false);
  assert.equal(h.requests("config-set").length, 0);
}
{
  const h = harness({ name: "commissioning" });
  await h.open();
  h.edit(9);
  await h.button("OK").onClick();
  assert.equal(h.dialogOpen(), false);
  h.setConfig({ address: 9, baud: 9600, communicationError: 263 });
  h.setState([-1, 9, null, null, false, 263]);
  await h.open();
  assert.equal(
    h.field().disabled,
    true,
    "absent current sensor cannot be reprogrammed",
  );
  assert.equal(
    h.connectField().value,
    "1",
    "factory sensor address is the default",
  );
  assert.equal(
    h.button("Connect").disabled,
    false,
    "old ID error does not block connection",
  );
  await h.button("Connect").onClick();
  assert.equal(
    h.dialogOpen(),
    true,
    "connection keeps physical ID editor open",
  );
  assert.equal(h.field().value, "1");
  assert.equal(h.field().disabled, false);
  h.edit(10);
  await h.button("OK").onClick();
  assert.equal(
    JSON.stringify(h.requests("config-set").map((call) => call.data)),
    '[{"address":9},{"connectAddress":1},{"address":10}]',
  );
  assert.equal(h.requests("config-set")[1].options.timeout, 15000);
  assert.equal(
    h.requests("config-get").length,
    5,
    "every action independently reloads settings",
  );
  assert(h.calls.every((call) => call.name === "commissioning"));
}

{
  const h = harness();
  await h.open();
  h.edit(9);
  const oldSave = h.button("OK");
  let finish;
  h.handler("config-set", async (data) => {
    assert.equal(
      JSON.stringify(data),
      '{"connectAddress":1}',
      "dirty physical ID is not submitted",
    );
    await new Promise((resolve) => {
      finish = resolve;
    });
    h.setConfig(normalConfig);
    return { data: normalConfig };
  });
  const connect = h.button("Connect"),
    first = connect.onClick();
  await connect.onClick();
  await oldSave.onClick();
  assert.equal(
    h.requests("config-set").length,
    1,
    "one shared guard covers connect and physical writes",
  );
  assert.equal(h.connectField().disabled, true);
  for (const label of ["Connect", "OK", "Refresh", "Cancel"])
    assert.equal(h.button(label).disabled, true);
  finish();
  await first;
  assert.equal(h.dialogOpen(), true);
  assert.equal(
    h.field().value,
    "1",
    "read-back replaces unsent physical-ID edits",
  );
}

{
  const h = harness();
  h.setConfig({ address: 9, baud: 9600, communicationError: 263 });
  h.setState([-1, 9, null, null, false, 263]);
  await h.open();
  h.handler("config-set", async () => {
    throw new Error("sensor probe timed out");
  });
  await h.button("Connect").onClick();
  assert.equal(
    h.field().value,
    "9",
    "a failed probe keeps the previous binding",
  );
  assert.equal(h.requests("config-set").length, 1);
  assert.equal(h.requests("config-get").length, 2);
  assert.match(h.visibleText(), /sensor probe timed out/);
  assert.equal(h.dialogOpen(), true);
  await h.button("Refresh").onClick();
  assert.equal(
    h.requests("config-set").length,
    1,
    "refresh does not repeat connect",
  );
}

{
  const h = harness();
  h.setConfig({ address: 9, baud: 9600, communicationError: 263 });
  h.setState([-1, 9, null, null, false, 263]);
  await h.open();
  h.handler("config-set", async () => {
    h.setConfig(normalConfig);
    h.setState([-1, 1, null, null, false, 0]);
    throw new Error("connect response lost");
  });
  await h.button("Connect").onClick();
  assert.equal(h.field().value, "1", "lost response reloads adopted binding");
  assert.equal(h.field().disabled, false);
  assert.match(h.visibleText(), /connect response lost/);
  assert.equal(h.dialogOpen(), true);
  assert.equal(
    h.requests("config-set").length,
    1,
    "lost response never repeats the action",
  );
}

{
  const h = harness();
  await h.open();
  for (const invalid of ["", "0", "248", "-1", "1.5", "1e1", " 1", "NaN"]) {
    h.editConnect(invalid);
    assert.equal(h.button("Connect").disabled, true, invalid);
    await h.button("Connect").onClick();
  }
  h.editConnect(247);
  assert.equal(h.button("Connect").disabled, false);
  for (const control of [
    { disabled: true },
    { busy: true },
    { known: false },
  ]) {
    Object.assign(
      h.control,
      { disabled: false, busy: false, known: true },
      control,
    );
    assert.equal(h.button("Connect").disabled, true);
  }
  Object.assign(h.control, { disabled: false, busy: false, known: true });
  h.setState([-1, 1, null, null, true, 263]);
  assert.equal(
    h.button("Connect").disabled,
    true,
    "pending state blocks binding changes",
  );
  h.setState(normalState);
  h.setConfig({ ...normalConfig, pendingAddress: 9 });
  await h.button("Refresh").onClick();
  assert.equal(
    h.button("Connect").disabled,
    true,
    "persisted pending change blocks connection",
  );
  assert.equal(h.requests("config-set").length, 0);
}

{
  const h = harness();
  await h.open();
  h.handler("config-get", async () => {
    throw new Error("connect read-back failed");
  });
  await h.button("Connect").onClick();
  assert.equal(
    h.button("Connect").disabled,
    true,
    "unknown configuration requires refresh",
  );
  assert.equal(h.button("OK").disabled, true);
  assert.match(h.visibleText(), /connect read-back failed/);
  h.clearHandler("config-get");
  await h.button("Refresh").onClick();
  assert.equal(h.button("Connect").disabled, false);
  assert.equal(h.requests("config-set").length, 1);
}

{
  const h = harness(),
    registry = {};
  vm.runInNewContext(
    `(function(exports, require) {${compiled.get("registry.ts")}\n})`,
    {},
  )(registry, require);
  registry.register(
    h.Csf09({ name: "soil-2", title: "Soil 2" }),
    h.Csf09({ name: "soil-3", title: "Soil 3" }),
  );
  const plugins = registry.getPlugins((plugin) => !!plugin.device);
  assert.equal(
    plugins.length,
    2,
    "real registry accepts separate named sensor instances",
  );
  assert.equal(
    JSON.stringify(plugins.map((plugin) => plugin.name)),
    '["soil-2","soil-3"]',
  );
  const container = {};
  const jsx = (type, props) => ({ type, props });
  vm.runInNewContext(
    `(function(exports, require) {${compiled.get("devices.tsx")}\n})`,
    {},
  )(container, (id) => {
    if (id === "react") return { createElement: jsx };
    if (id === "react/jsx-runtime") return { jsx, jsxs: jsx };
    if (id === "@mui/material") return { Grid: "Grid" };
    if (id === "@ts-libs/plugins") return registry;
    throw new Error(`Unexpected devices dependency: ${id}`);
  });
  const widgets = container.default().props.children;
  assert.equal(
    widgets.length,
    2,
    "production device container creates both cards",
  );
  for (const [index, widget] of widgets.entries()) {
    assert.equal(widget.type, h.Content);
    h.setProps(widget.props);
    assert.equal(h.find("CardBox").title, `Soil ${index + 2}`);
    assert.equal(h.queries.at(-1), `soil-${index + 2}`);
    assert.equal(h.deviceNames.at(-1), `soil-${index + 2}`);
  }
}

{
  const h = harness();
  h.setState(undefined);
  h.setStateError('unsupported-state');
  assert.match(h.text(h.render()), /This firmware does not provide state for this sensor/);
  assert.match(h.text(h.render()), /Unavailable/);
}

console.log(
  "CSF09 UI: 19 groups passed (production TSX; measurements, physical ID, local connection, validation, duplicate guards, read-back, recovery, real multi-device registry, unavailable backend).",
);
