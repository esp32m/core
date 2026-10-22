// Run the real TSX and MessageBox helper with deterministic React/transport doubles.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";
import path from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, "../..");
const require = createRequire(path.join(core, "web-ui/package.json"));
const babel = require("@babel/core");
const ui = path.join(core, "web-ui/esp32m/ui/src/dev/bfs");
const compiled = new Map();
for (const filename of [
  path.join(ui, "Content.tsx"),
  path.join(ui, "values.ts"),
  path.join(ui, "types.ts"),
  path.join(ui, "catalog.ts"),
  path.join(core, "web-ui/ts-libs/ui-base/src/web/messagebox.tsx"),
]) {
  const result = await babel.transformAsync(readFileSync(filename, "utf8"), {
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
  });
  compiled.set(path.basename(filename), result.code);
}

const channel = (raw, value = raw) => ({ raw, value, sampledAtMs: 999950, valid: true });
const fixture = JSON.parse(
  readFileSync(path.join(here, "fixture.json"), "utf8"),
);
const normalState = fixture.state;
const normalParameters = fixture.parameters.parameters;
const tick = () => new Promise((resolve) => setImmediate(resolve));

function harness(initial = {}) {
  let state = { ...normalState, ...initial };
  let parameters = structuredClone(normalParameters);
  let cursor = 0;
  const hooks = [];
  const calls = [];
  const errors = [];
  const jsx = (type, props) => ({ type, props: props || {} });
  const react = {
    Component: class {},
    createElement: (type, props, ...children) =>
      jsx(type, { ...props, ...(children.length ? { children } : {}) }),
    useState(initialValue) {
      const index = cursor++;
      if (!(index in hooks)) hooks[index] = initialValue;
      return [
        hooks[index],
        (value) => {
          hooks[index] =
            typeof value === "function" ? value(hooks[index]) : value;
        },
      ];
    },
    useRef(initialValue) {
      const index = cursor++;
      if (!(index in hooks)) hooks[index] = { current: initialValue };
      return hooks[index];
    },
    useEffect() {},
  };
  const api = {
    async getState(name) {
      calls.push({ method: "getState", name });
      return { data: state };
    },
    async request(name, command, data, options) {
      calls.push({ name, command, data, options });
      if (command === "run")
        return { data: { accepted: true, verified: true } };
      if (command === "config-get")
        return { data: { address: state.address, baud: state.baud } };
      if (command === "config-set") {
        state = { ...state, ...data };
        return { data };
      }
      if (command === "faults")
        return { data: structuredClone(fixture.faults) };
      if (command === "parameter-cache")
        return { data: { parameters: structuredClone(parameters), cached: true } };
      if (command === "parameters")
        return {
          data: {
            parameters: structuredClone(
              parameters.filter(
                (p) => data.address === undefined || p.address === data.address,
              ),
            ),
          },
        };
      if (command === "parameter-set") {
        const parameter = parameters.find((p) => p.address === data.address);
        assert.equal(data.expectedRaw, parameter.raw);
        parameter.raw = data.raw;
        return { data: { accepted: true, verified: true, raw: data.raw } };
      }
      throw new Error(`Unexpected command: ${command}`);
    },
  };
  const modules = {};
  const context = vm.createContext({ console, Date });
  const load = (filename) => {
    if (modules[filename]) return modules[filename];
    const exports = {};
    modules[filename] = exports;
    const execute = vm.runInContext(
      `(function(exports, require) {${compiled.get(filename)}\n})`,
      context,
      { filename },
    );
    execute(exports, (id) => {
      if (id === "react") return react;
      if (id === '../DeviceControl') return {
        useDeviceControl: () => ({ disabled: false, busy: false, known: true }),
        DeviceDisabledAlert: () => null, DeviceDisableSetting: () => null,
      };
      if (id === "react/jsx-runtime")
        return { jsx, jsxs: jsx, Fragment: "Fragment" };
      if (id === "@mui/material" || id === "@mui/icons-material")
        return new Proxy({}, { get: (_, key) => key });
      if (id === "@ts-libs/ui-app") return { CardBox: "CardBox" };
      if (id === "@ts-libs/ui-base") return load("messagebox.tsx");
      if (id === "@ts-libs/ui-i18n")
        return { useTranslation: () => ({ tr: (value) => value }) };
      if (id === "@ts-libs/ui-snack")
        return {
          useSnackApi: () => ({ error: (error) => errors.push(error) }),
        };
      if (id === "../../app") return { NameValueList: "NameValueList" };
      if (id === "../..")
        return { useBackendApi: () => api, useModuleState: () => state, useSampleAger: () => stamp => typeof stamp === "number" && stamp >= 0 ? 1000000 - stamp : -1 };
      if (id === "./types") return load("types.ts");
      if (id === "./values") return load("values.ts");
      if (id === "./catalog") return load("catalog.ts");
      throw new Error(`Unexpected dependency: ${id}`);
    });
    return exports;
  };
  const component = load("Content.tsx");
  const render = () => {
    cursor = 0;
    return component.Content({ name: "bfs-test" });
  };
  const text = (node) => {
    if (Array.isArray(node)) return node.map(text).join("");
    if (node == null || typeof node === "boolean") return "";
    if (typeof node === "object") {
      if (node.type === "Dialog" && !node.props.open) return "";
      return text(node.props?.children);
    }
    return String(node);
  };
  const all = () => {
    const result = [];
    const visit = (node) => {
      if (Array.isArray(node)) return node.forEach(visit);
      if (!node || typeof node !== "object") return;
      if (typeof node.type === "function") return visit(node.type(node.props));
      if (node.type === "Dialog" && !node.props.open) return;
      result.push(node);
      visit(node.props.children);
      visit(node.props.action);
    };
    visit(render());
    return result;
  };
  const find = (type, predicate = () => true) => {
    const found = all().find(
      (node) => node.type === type && predicate(node.props, node),
    );
    assert(found, `Missing visible ${type}`);
    return found.props;
  };
  return {
    api,
    calls,
    errors,
    find,
    all,
    text,
    render,
    values: load("values.ts"),
    setState(update) {
      state = { ...state, ...update };
    },
    setParameters(next) {
      parameters = structuredClone(next);
    },
    field(label) {
      return find("TextField", (props) => props.label === label);
    },
    button(label) {
      return find("Button", (_, node) => text(node) === label);
    },
    icon(label) {
      return find("IconButton", (props) => props["aria-label"] === label);
    },
    row(label) {
      return find("NameValueList").list.find(([name]) => name === label)?.[1];
    },
    requests(command) {
      return calls.filter((call) => call.command === command);
    },
    visibleText() {
      return all().map(text).join("\n");
    },
  };
}

{
  const h = harness();
  assert.equal(h.row("Water pressure (set / current)"), "3.0 / 2.4 bar");
  assert.equal(h.row("Output frequency (set / actual)"), "35.00 / 0.00 Hz");
  assert.equal(h.row("Module temperature (estimate)"), "Approx. 24.7 \u00b0C");
  assert.equal(
    h.all().filter((node) => node.type === "TextField").length,
    0,
    "no in-card setpoint editor",
  );
  h.setState({
    pressure: { ...channel(22, 2.2), sampledAtMs: 989999 },
    status: { valid: false, sampledAtMs: -1 },
  });
  assert.equal(h.row("Water pressure (set / current)"), "3.0 / -- bar");
  assert.equal(h.row("Drive state"), "Unknown");
  assert.equal(h.button("Stop").disabled, false);
  assert.equal(
    h.all().some((node) => node.type === "Button" && h.text(node) === "Start"),
    false,
  );
  assert.equal(h.calls.length, 0, "normal rendering only subscribes to state");
}

{
  const h = harness();
  h.button("Start").onClick();
  assert.equal(h.requests("run").length, 0);
  h.button("cancel").onClick();
  assert.equal(h.requests("run").length, 0, "cancel never starts");
  h.button("Start").onClick();
  let finish;
  const original = h.api.request;
  h.api.request = async (...args) => {
    if (args[1] === "run")
      await new Promise((resolve) => {
        finish = resolve;
      });
    return original(...args);
  };
  const confirm = h.button("ok");
  confirm.onClick();
  confirm.onClick();
  finish();
  await tick();
  assert.equal(h.requests("run").length, 1, "double confirmation sends once");
  assert.equal(
    JSON.stringify(h.requests("run")[0].data),
    '{"running":true,"confirm":true}',
  );
  assert.equal(h.requests("run")[0].name, "bfs-test");
  h.api.request = original;
  h.setState({ status: channel(1) });
  await h.button("Stop").onClick();
  assert.equal(JSON.stringify(h.requests("run")[1].data), '{"running":false}');
}

{
  const h = harness();
  h.button("Start").onClick();
  h.setState({ status: { valid: false, sampledAtMs: -1 } });
  h.button("ok").onClick();
  await tick();
  assert.equal(
    h.requests("run").length,
    0,
    "confirmation checks latest status",
  );
  assert.match(h.visibleText(), /current STOP status/);
  const original = h.api.request;
  h.api.request = async (...args) => {
    if (args[1] === "run") {
      h.calls.push({ command: "run" });
      throw new Error("unknown result");
    }
    return original(...args);
  };
  h.button("cancel").onClick();
  await h.button("Stop").onClick();
  assert.equal(
    h.requests("run").length,
    1,
    "uncertain action is never retried",
  );
  assert.equal(h.errors.length, 1);
}

{
  const h = harness();
  await h.button("Fault info").onClick();
  assert.equal(h.requests("faults").length, 1);
  assert.match(h.visibleText(), /Historical fault records/);
  assert.match(h.visibleText(), /EHU2: Overvoltage during deceleration/);
  assert.match(h.visibleText(), /Approx\. 48\.41 Hz/);
  assert.match(h.visibleText(), /Deceleration.*Overvoltage protection/);
  h.render();
  h.render();
  assert.equal(h.requests("faults").length, 1, "fault history is on demand");
  assert.equal(
    h.calls.some((call) => call.command === "info-get"),
    false,
  );
}

{
  const h = harness();
  await h.icon("Configure BFS pump").onClick();
  for (const invalid of ["0", "248", "1.5", "NaN", ""]) {
    h.field("Modbus slave address").onChange({ target: { value: invalid } });
    assert.equal(h.button("OK").disabled, true, invalid);
  }
  h.field("Modbus slave address").onChange({ target: { value: "2" } });
  h.field("Baud rate").onChange({ target: { value: "19200" } });
  await h.button("OK").onClick();
  assert.equal(
    JSON.stringify(h.requests("config-set")[0].data),
    '{"address":2,"baud":19200}',
  );
  assert.equal(h.requests("config-set")[0].options.timeout, 45000);
  assert.equal(h.requests("parameter-set").length, 0);
}

{
  const h = harness();
  await h.button("Configuration").onClick();
  assert.equal(h.requests("parameters").length, 0, "opening never starts a bus scan");
  assert.equal(h.requests("parameter-cache").length, 1);
  assert.match(h.visibleText(), /Cached/);
  assert.equal(h.icon("Edit 0x0017").disabled, false);
  assert.equal(
    h
      .all()
      .some(
        (node) =>
          node.type === "IconButton" &&
          node.props["aria-label"] === "Edit F3-07",
      ),
    false,
  );
  for (const id of ["F0-12", "F2-02", "F3-12", "F4-11"])
    assert.equal(h.icon(`Edit ${id}`).disabled, true, id);
  assert.match(h.visibleText(), /Raw 800 \(0x0320\)/);
  assert.equal(
    h
      .all()
      .filter(
        (node) =>
          node.type === "IconButton" &&
          node.props["aria-label"]?.startsWith("Edit F"),
      ).length,
    79,
    "all real registry rows render under full group labels",
  );
  const edit = h.icon("Edit F1-02");
  assert.equal(
    h.text({ props: { children: edit.children } }),
    "",
    "edit action is icon-only",
  );
  await edit.onClick();
  assert.equal(h.field("Value (s)").value, "7.5");
  for (const invalid of ["", "-1", "0", "60.1", "7.55", "1e1"]) {
    h.field("Value (s)").onChange({ target: { value: invalid } });
    assert.equal(h.button("OK").disabled, true, invalid);
  }
  h.field("Value (s)").onChange({ target: { value: "8.2" } });
  const save = h.button("OK");
  const pending = save.onClick();
  await save.onClick();
  await pending;
  assert.equal(h.requests("parameter-set").length, 1);
  assert.equal(
    JSON.stringify(h.requests("parameter-set")[0].data),
    '{"address":258,"raw":82,"expectedRaw":75}',
  );
  assert.equal(h.requests("parameters").length, 2);
  assert.equal(
    JSON.stringify(h.requests("parameters")[1].data),
    '{"address":258}',
  );
  assert.match(h.visibleText(), /8\.2 s/);
  assert.equal(h.icon("Edit F1-02").disabled, false);
  await h.icon("Edit F1-02").onClick();
  assert.equal(h.field("Value (s)").value, "8.2", "single read retains scale and unit");
  h.field("Value (s)").onChange({ target: { value: "60.1" } });
  assert.equal(h.button("OK").disabled, true, "single read retains bounds");
}

{
  for (const raw of [0, 1, 2, 3]) {
    const h = harness();
    h.setParameters(normalParameters.map((p) =>
      p.address === 0x0204 ? { ...p, raw: (raw + 1) % 4 } : p));
    await h.button("Configuration").onClick();
    assert.equal(h.icon("Edit F2-04").disabled, false);
    await h.icon("Edit F2-04").onClick();
    assert.equal(!!h.field("Value").select, false, "no invented option labels");
    for (const invalid of ["", "-1", "4", "65535", "1.5"]) {
      h.field("Value").onChange({ target: { value: invalid } });
      assert.equal(h.button("OK").disabled, true, invalid);
    }
    h.field("Value").onChange({ target: { value: String(raw) } });
    assert.equal(h.button("OK").disabled, false);
    await h.button("OK").onClick();
    assert.equal(h.requests("parameter-set").length, 1);
    assert.equal(h.requests("parameter-set")[0].data.address, 0x0204);
    assert.equal(h.requests("parameter-set")[0].data.raw, raw);
    assert.equal(h.requests("parameters").at(-1).data.address, 0x0204);
  }
}

{
  const h = harness();
  await h.button("Configuration").onClick();
  await h.icon("Edit F0-19").onClick();
  assert.equal(h.field("Value").select, true);
  h.field("Value").onChange({ target: { value: "1" } });
  await h.button("OK").onClick();
  assert.equal(
    h.requests("parameter-set")[0].data.raw,
    1,
    "enum values use raw integers",
  );
  await h.icon("Edit F1-02").onClick();
  h.field("Value (s)").onChange({ target: { value: "8" } });
  const original = h.api.request;
  h.api.request = async (...args) => {
    if (args[1] === "parameter-set") {
      h.calls.push({
        name: args[0],
        command: args[1],
        data: args[2],
        options: args[3],
      });
      throw new Error("write timeout");
    }
    return original(...args);
  };
  await h.button("OK").onClick();
  assert.match(h.visibleText(), /write timeout Current value refreshed/);
  assert.equal(
    h.field("Value (s)").value,
    "8",
    "preserve edited value after failed write",
  );
  assert.equal(h.requests("parameters").at(-1).data.address, 0x0102);
  assert.equal(h.requests("parameter-set").length, 2, "no automatic retry");
}

{
  const h = harness();
  await h.button("Configuration").onClick();
  await h.icon("Edit F1-02").onClick();
  h.field("Value (s)").onChange({ target: { value: "8" } });
  const original = h.api.request;
  h.api.request = async (...args) => {
    if (args[1] === "parameters") throw new Error("read-back timeout");
    return original(...args);
  };
  await h.button("OK").onClick();
  assert.match(h.visibleText(), /read-back timeout/);
  assert.equal(
    h.button("OK").disabled,
    true,
    "unknown register cannot be written again",
  );
  assert.equal(h.field("Value (s)").disabled, true);
  assert.equal(h.requests("parameter-set").length, 1);
}

{
  const h = harness();
  const signed = {
    ...h.values.decodeParameter(normalParameters.find((p) => p.address === 0x0102)),
    signed: true,
    scale: 0.1,
    min: -100,
    max: 100,
    raw: 0,
  };
  assert.equal(h.values.encodeInput(signed, "-12.3").raw, 65413);
  assert.equal(h.values.numericValue(signed, 65413), -12.3);
  assert.equal(
    h.values.encodeInput({ ...signed, writable: false }, "-12.3").raw,
    undefined,
  );
  assert.equal(
    h.values.encodeInput({ ...signed, scale: undefined }, "2").raw,
    undefined,
  );
  assert.equal(
    h.values.faultText(18, { address: 0x1012, raw: 23 }),
    "Unknown fault (23)",
  );
  assert.equal(
    h.values.faultText(18, { address: 0x1012, error: 263 }),
    "Unavailable",
  );
}

{
  const h = harness();
  const original = h.api.request;
  h.api.request = async (...args) =>
    args[1] === "run"
      ? { data: { accepted: true, verified: false, readError: 263 } }
      : original(...args);
  h.button("Start").onClick();
  h.button("ok").onClick();
  await tick();
  assert.match(h.visibleText(), /RUN was not verified.*Status error: 263/);
  h.button("cancel").onClick();
  h.setState({ status: channel(1) });
  await h.button("Stop").onClick();
  assert.match(String(h.errors[0]), /STOP was not verified/);
}

{
  const h = harness();
  const readings = [
    { address: 0x0102, error: 263 },
    { address: 0x0307, raw: 1, id: "Forged", writable: true },
    { address: 0xffff, raw: 1, id: "Unknown", writable: true },
  ];
  h.setParameters(readings);
  await h.button("Configuration").onClick();
  assert.equal(h.icon("Edit F1-02").disabled, false, "failed read can be retried via Edit");
  assert.equal(h.icon("Edit F0-00").disabled, false, "missing value can be fetched via Edit");
  await h.icon("Edit F1-02").onClick();
  assert.equal(h.button("OK").disabled, true, "no write without a fresh reading");
  h.button("Cancel").onClick();
  assert.equal(h.visibleText().includes("Forged"), false);
  assert.equal(h.visibleText().includes("Unknown"), false);
  h.setParameters(normalParameters);
  await h.icon("Refresh F1-02").onClick();
  assert.equal(h.icon("Edit F1-02").disabled, false, "fresh read clears previous error");
  await h.icon("Edit F1-02").onClick();
  assert.equal(h.field("Value (s)").value, "7.5");
  const decoded = h.values.decodeParameter({ address: 0x0102, raw: 82,
    scale: 100, unit: "bad", writable: false, name: "Forged" });
  assert.equal(decoded.scale, 0.1, "metadata comes only from checked-in catalog");
  assert.equal(decoded.unit, "s");
  assert.equal(decoded.error, undefined, "single reading does not retain stale errors");
  assert.equal(h.values.decodeParameter({ address: 0xffff, raw: 1 }), undefined);
  const duplicate = h.values.decodeParameters([
    { address: 0x0102, raw: 75 }, { address: 0x0102, raw: 82 },
  ]).find((p) => p.address === 0x0102);
  assert.equal(h.values.editable(duplicate), false, "duplicate readings fail closed");
}

for (const [button, command, key] of [
  ['Fault info', 'faults', 'values'],
]) {
  const h = harness();
  const original = h.api.request;
  h.api.request = async (...args) => {
    const response = await original(...args);
    if (args[1] !== command) return response;
    const values = response.data[key];
    return { data: {
      aborted: true, abortReason: 'failures',
      [key]: values.map((v, i) => i < 2 ? v : i < 5
        ? { address: v.address, error: 263 }
        : { address: v.address, error: 259, skipped: true }),
    } };
  };
  await h.button(button).onClick();
  assert.match(h.visibleText(), /Read stopped after three consecutive communication failures/);
  assert.match(h.visibleText(), /2 of (10|79) values received/);
  assert.match(h.visibleText(), /Not read/);
  assert(h.all().some(n => n.type === 'Tooltip' && n.props.title === 'Response timed out'));
  assert(h.all().some(n => n.type === 'Tooltip' && n.props.title === 'Not attempted because the scan stopped'));
  assert(h.requests(command)[0].options.timeout > 20_000,
    'response timeout allows the 20 s scan budget plus an in-flight transaction');
  h.api.request = original;
  await h.button('Refresh').onClick();
  assert(!h.visibleText().includes('Read stopped'), 'successful refresh clears warning');
  assert(!h.visibleText().includes('Not read'), 'successful refresh clears skipped rows');
  assert.match(h.values.dialogReadWarning({ aborted: true }, []), /before all registers/,
    'old firmware without abortReason remains supported');
}
{
  const h = harness();
  await h.button('Configuration').onClick();
  h.setParameters(normalParameters.map(p => p.address === 0x0017 ? { ...p, raw: 34 } : p));
  let finish;
  const original = h.api.request;
  h.api.request = async (...args) => {
    if (args[1] === 'parameters') await new Promise(resolve => { finish = resolve; });
    return original(...args);
  };
  const edit = h.icon('Edit 0x0017').onClick();
  assert.equal(h.button('OK').disabled, true, 'cached value cannot submit while refreshing');
  finish(); await edit;
  h.api.request = original;
  assert.equal(h.field('Value (bar)').value, '3.4', 'edit uses newly read value, not cached 3.0');
  h.field('Value (bar)').onChange({ target: { value: '3.5' } });
  await h.button('OK').onClick();
  assert.equal(h.requests('parameter-set')[0].data.expectedRaw, 34);
  assert.equal(h.requests('parameter-set')[0].data.raw, 35);
  assert.equal(h.requests('parameters').length, 2, 'only edit and post-write single reads');
}
{
  const h = harness();
  h.setParameters([]);
  await h.button('Configuration').onClick();
  assert.match(h.visibleText(), /Not read/);
  h.setParameters(normalParameters);
  await h.icon('Edit F1-02').onClick();
  assert.equal(h.field('Value (s)').value, '7.5', 'uncached parameter can be edited directly');
  h.button('Cancel').onClick();
  await h.icon('Refresh F1').onClick();
  assert.equal(h.requests('parameters').length, 13);
  assert(h.requests('parameters').every(r => r.data.address >= 0x0100 && r.data.address <= 0x010b));
  assert.equal(h.requests('parameter-set').length, 0);
}
{
  const h = harness();
  await h.button('Configuration').onClick();
  const original = h.api.request;
  let finish;
  h.api.request = async (...args) => {
    if (args[1] === 'parameters') await new Promise(resolve => { finish = resolve; });
    return original(...args);
  };
  const refresh = h.button('Refresh all').onClick();
  h.button('Cancel refresh').onClick();
  finish(); await refresh;
  assert.equal(h.requests('parameters').length, 1, 'cancel stops before another register');
  assert.match(h.visibleText(), /Cancelled/);
  let attempts = 0;
  h.api.request = async (...args) => {
    if (args[1] === 'parameters') { ++attempts; throw new Error('offline'); }
    return original(...args);
  };
  await h.button('Refresh all').onClick();
  assert.equal(attempts, 3, 'offline refresh is bounded');
  assert.match(h.visibleText(), /Previous values retained/);
  assert.match(h.visibleText(), /7\.5 s/, 'cached values survive refresh failures');
  assert.match(h.visibleText(), /Refresh failed/);
}
console.log('BFS UI: existing interactions plus cached/open, fresh-edit, pressure, missing-cache, category, cancellation and offline tests passed.');
