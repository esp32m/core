// Executes the actual TSX component with deterministic hook/transport doubles.
// No DOM, server, meter, or protocol writes are used by this harness.
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
const source = path.join(core, "web-ui/esp32m/ui/src/dev/sm10/Content.tsx");
const compiled = (
  await babel.transformAsync(readFileSync(source, "utf8"), {
    filename: source,
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
).code;

const normalState = {
  sampledAtMs: 999900,
  address: 1,
  baud: 9600,
  flow: 0,
  total: 0.0073650787,
  rtc: "2026-09-24T10:00:25",
  rtcValid: true,
  rtcSampledAtMs: 999500,
  rtcSync: "synced",
  status: 0,
  temperature: 22.8,
  voltage: 3.698,
  bootCount: 1090,
};

function harness(initial = {}) {
  let state = { ...normalState, ...initial };
  let cursor = 0;
  const hooks = [];
  const calls = [];
  const errors = [];
  const api = {
    async getConfig(name) {
      calls.push({ method: "getConfig", name });
      return { data: { address: state.address, baud: state.baud } };
    },
    async getState(name) {
      calls.push({ method: "getState", name });
      return { data: state };
    },
    async request(name, command, data, options) {
      calls.push({ method: "request", name, command, data, options });
      return {
        data:
          command === "history"
            ? {
                bank: data.bank,
                values: [0, 0.0071, 0.0073],
                rtc: state.rtc,
                current: state.total,
              }
            : state,
      };
    },
  };
  const jsx = (type, props) => ({ type, props: props || {} });
  const exports = {};
  vm.runInNewContext(
    compiled,
    {
      exports,
      console,
      Date,
      require(id) {
        if (id === "react")
          return {
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
          };
        if (id === '../DeviceControl') return {
        useDeviceControl: () => ({ disabled: false, busy: false, known: true }),
        DeviceDisabledAlert: () => null, DeviceDisableSetting: () => null,
      };
      if (id === "react/jsx-runtime")
          return { jsx, jsxs: jsx, Fragment: "Fragment" };
        if (id === "@mui/material" || id === "@mui/icons-material")
          return new Proxy({}, { get: (_, key) => key });
        if (id === "@ts-libs/ui-app") return { CardBox: "CardBox" };
        if (id === "@ts-libs/ui-snack")
          return { useSnackApi: () => ({ error: (e) => errors.push(e) }) };
        if (id === "../../app") return { NameValueList: "NameValueList" };
        if (id === "../..")
          return { useBackendApi: () => api, useModuleState: () => state, useSampleAge: stamp => typeof stamp === "number" && stamp >= 0 ? 1000000 - stamp : -1 };
        if (id === "./types") return { Name: "SM10" };
        throw new Error(`Unexpected dependency: ${id}`);
      },
    },
    { filename: source },
  );
  const render = () => {
    cursor = 0;
    return exports.Content({ name: "SM10-test" });
  };
  const all = () => {
    const result = [];
    const visit = (node) => {
      if (Array.isArray(node)) return node.forEach(visit);
      if (!node || typeof node !== "object") return;
      if (node.type === "Dialog" && !node.props.open) return;
      result.push(node);
      visit(node.props.children);
      visit(node.props.action);
    };
    visit(render());
    return result;
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
  const find = (type, predicate) => {
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
    all,
    text,
    find,
    render,
    setState(update) {
      state = { ...state, ...update };
    },
    button(label) {
      return find("Button", (_, node) => text(node) === label);
    },
    field(label) {
      return find("TextField", (props) => props.label === label);
    },
    row(label) {
      return find("NameValueList", () => true).list.find(
        ([name]) => name === label,
      )?.[1];
    },
    requests(command) {
      return calls.filter((call) => call.command === command);
    },
    visibleText() {
      return all().map(text).join("\n");
    },
    async settings() {
      await find(
        "IconButton",
        (props) => props["aria-label"] === "Configure SM10",
      ).onClick();
    },
  };
}

{
  const h = harness({ status: undefined, bootCount: undefined });
  assert.equal(h.row("Status"), "Unavailable");
  assert.equal(h.row("Boot count"), "Unavailable");
  assert.equal(h.row("Total"), "0.00736508 m³");
  assert.equal(h.row("Meter RTC"), "2026-09-24 10:00:25");
  h.setState({ rtcValid: false, rtcSync: "invalid" });
  assert.equal(h.row("Meter RTC"), "Invalid calendar");
  assert.equal(
    h.row("RTC synchronization"),
    "Invalid calendar; synchronization pending",
  );
  h.setState({ sampledAtMs: -1, rtcSampledAtMs: 879999 });
  assert.equal(h.row("Total"), "Unavailable");
  assert.equal(h.row("Flow"), "Unavailable");
  assert.equal(h.row("Meter RTC"), "Unavailable");
  assert.equal(h.button("History snapshots").disabled, false);
  assert.equal(h.calls.length, 0);
}

{
  const h = harness();
  h.render();
  h.setState({ total: 1 });
  h.render();
  assert.equal(
    h.requests("history").length,
    0,
    "ordinary renders must not poll histories",
  );
  h.button("History snapshots").onClick();
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(h.requests("history").length, 1);
  assert.equal(h.requests("history")[0].data.bank, "hourly");
  assert.match(h.visibleText(), /3 \(newest\).*0\.0073 m³/s);
  h.field("History bank").onChange({ target: { value: "yearly" } });
  assert.equal(h.requests("history").length, 1);
  await h.button("Load snapshots").onClick();
  assert.equal(h.requests("history")[1].data.bank, "yearly");
  h.render();
  assert.equal(h.requests("history").length, 2);
  assert.equal(h.requests("set-total").length, 0);
}

{
  const h = harness();
  await h.settings();
  h.field("Modbus slave ID").onChange({ target: { value: "2" } });
  assert.equal(
    h.button("Save settings").disabled,
    true,
    "fraction-loss acknowledgment required",
  );
  h.find("FormControlLabel", () => true).control.props.onChange(null, true);
  h.field("Modbus slave ID").onChange({ target: { value: "235" } });
  assert.equal(
    h.button("Save settings").disabled,
    true,
    "0xEB collision excluded",
  );
  h.field("Modbus slave ID").onChange({ target: { value: "2" } });
  h.field("Baud rate").onChange({ target: { value: "2400" } });
  await h.button("Save settings").onClick();
  const request = h.requests("config-set")[0];
  assert.equal(request.name, "SM10-test");
  assert.equal(
    JSON.stringify(request.data),
    '{"address":2,"baud":2400,"confirm":true}',
  );
  assert.equal(request.options.timeout, 45000);
  assert.equal(h.requests("config-set").length, 1);
  assert.equal(
    h.all().some((node) => node.type === "Dialog"),
    false,
  );
}

{
  const h = harness();
  h.api.getConfig = async () => {
    throw new Error("offline");
  };
  await h.settings();
  h.field("Modbus slave ID").onChange({ target: { value: "2" } });
  h.find("FormControlLabel", () => true).control.props.onChange(null, true);
  assert.equal(
    h.button("Save settings").disabled,
    true,
    "unknown settings must not be written",
  );
  assert.equal(h.requests("config-set").length, 0);
}

{
  const h = harness();
  h.button("Change total").onClick();
  for (const invalid of ["", "-1", "1e3", "0.00001", "10000", "NaN"]) {
    h.field("New cumulative total (m³)").onChange({
      target: { value: invalid },
    });
    assert.equal(h.button("Review change").disabled, true, invalid);
    assert.equal(
      h.requests("set-total").length,
      0,
      `invalid total ${JSON.stringify(invalid)} must not be written`,
    );
  }
}

for (const total of [
  "0.2",
  "0.2000",
  "0.0001",
  "4",
  "9999.9",
  "9999.9999",
  "12.3456",
]) {
  const h = harness();
  h.button("Change total").onClick();
  h.field("New cumulative total (m³)").onChange({
    target: { value: total },
  });
  assert.equal(h.button("Review change").disabled, false, total);
  assert.equal(h.requests("set-total").length, 0, "editing is not a write");
  h.button("Review change").onClick();
  assert.equal(h.requests("set-total").length, 0, "review is not a write");
  assert(h.visibleText().includes(`${Number(total).toFixed(4)} m³`), total);
  await h.button("Confirm change total").onClick();
  const request = h.requests("set-total")[0];
  assert.equal(request.name, "SM10-test");
  assert.equal(typeof request.data.total, "string", total);
  assert.equal(request.data.total, total, "preserve the typed decimal exactly");
  assert.equal(
    JSON.stringify(request.data),
    `{"total":"${total}","confirm":true}`,
    "JSON transport must not round decimal text",
  );
  assert.equal(request.options.timeout, 45000);
  assert.equal(h.requests("set-total").length, 1);
  assert.equal(
    h.all().some((node) => node.type === "Dialog"),
    false,
  );
}

{
  const h = harness();
  h.button("Change total").onClick();
  h.field("New cumulative total (m³)").onChange({ target: { value: "0" } });
  h.button("Review change").onClick();
  let fail;
  h.api.request = async (_, command) => {
    h.calls.push({ command });
    await new Promise((_, reject) => {
      fail = reject;
    });
  };
  const pending = h.button("Confirm change total").onClick();
  assert.equal(h.button("Updating meter…").disabled, true);
  fail(new Error("response lost"));
  await pending;
  assert.match(h.visibleText(), /could not be verified/);
  assert.equal(
    h.requests("set-total").length,
    1,
    "uncertain writes must not auto-retry",
  );
  assert.equal(h.errors.length, 1);
  assert(h.button("Back"));
}

console.log(
  "SM10 UI: 6 interaction groups and 7 exact decimal submissions passed (actual TSX, mocked hooks/transport).",
);
