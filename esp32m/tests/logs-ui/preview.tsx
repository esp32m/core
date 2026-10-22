import { createRoot } from "react-dom/client";
import { Provider } from "react-redux";
import { createStore } from "redux";
import {
  Alert,
  CssBaseline,
  ThemeProvider,
  Typography,
  createTheme,
} from "@mui/material";
import i18next from "i18next";
import { initReactI18next } from "react-i18next";
import { DiscreteStatus } from "@ts-libs/tools";
import { ClientContext } from "../../web-ui/esp32m/ui/src/backend/hooks";
import {
  ConnectionStatus,
  IBackendApi,
} from "../../web-ui/esp32m/ui/src/backend/types";
import { Content } from "../../web-ui/esp32m/ui/src/system/content";
import { i18n } from "../../web-ui/esp32m/ui/src/system/i18n";
import { makeFixture } from "./fixture.mjs";

const query = new URLSearchParams(window.location.search);
const fixture = makeFixture(query.get("mode") || "");
const initialState: Record<string, any> = {
  app: fixture.app,
  ESP32: fixture.hardware,
  logs: fixture.state,
  _live: {},
};
const store = createStore(
  (
    state = initialState,
    action: { type: string; target?: string; value?: unknown },
  ) => {
    if (action.type === "fixture-state" && action.target)
      return { ...state, [action.target]: action.value };
    if (action.type === "fixture-live" && action.target)
      return {
        ...state,
        _live: { ...state._live, [action.target]: action.value },
      };
    return state;
  },
);
const requests: unknown[] = [];
const subscriptions: unknown[] = [];
const modules = new Map();
const pushState = (changes: Record<string, unknown>) => {
  fixture.state = { ...fixture.state, ...changes };
  store.dispatch({
    type: "fixture-state",
    target: "logs",
    value: fixture.state,
  });
};
const api = {
  status: new DiscreteStatus(ConnectionStatus.Connected),
  module(name: string) {
    if (!modules.has(name)) {
      let refs = 0;
      modules.set(name, {
        selectors: { state: (state: Record<string, unknown>) => state[name] },
        stateSelector: () => (state: Record<string, unknown>) => state[name],
        liveSelector: () => (state: Record<string, any>) =>
          state._live[name] === true,
        errorSelector: () => () => undefined,
        epochSelector: () => () => 0,
        useState: () => {
          if (++refs === 1) {
            subscriptions.push({ target: name, operation: "subscribe" });
            store.dispatch({ type: "fixture-live", target: name, value: true });
          }
          return () => {
            if (--refs === 0) {
              subscriptions.push({ target: name, operation: "unsubscribe" });
              store.dispatch({
                type: "fixture-live",
                target: name,
                value: false,
              });
            }
          };
        },
      });
    }
    return modules.get(name);
  },
  async request(
    target: string,
    command: string,
    data: Record<string, any> = {},
  ) {
    requests.push({ target, command, data });
    if (target === "app" && command === "describe")
      return {
        data: fixture.mode === "unsupported" ? {} : { logs: { schema: 1 } },
      };
    if (target === "app" && command === "config-get")
      return {
        data: {
          hostname: "logs-fixture",
          udplog: { enabled: false, host: "" },
        },
      };
    if (target === "ui" && command === "config-get")
      return {
        data: {
          auth: { enabled: false, username: "fixture", password: "fixture" },
        },
      };
    if (target === "ESP32" && command === "config-get") return { data: {} };
    if (target === "ota" && command === "config-get") return { data: {} };
    if (target !== "logs")
      throw new Error(`Unsupported fixture request: ${target}/${command}`);
    if (command === "info-get") return { data: fixture.info };
    if (command === "state-get") return { data: fixture.state };
    if (command === "config-get") return { data: fixture.config };
    if (command === "config-set") {
      fixture.config = { ...fixture.config, ...data };
      fixture.state = {
        ...fixture.state,
        ...fixture.config,
        effectiveQuotaBytes: Math.max(
          0,
          Math.min(
            fixture.config.quotaBytes,
            fixture.info.freeBytes +
              fixture.state.usedBytes -
              fixture.info.reserveBytes,
          ),
        ),
      };
      store.dispatch({
        type: "fixture-state",
        target: "logs",
        value: fixture.state,
      });
      return { data: fixture.config };
    }
    if (command === "list")
      return { data: { generation: "fixture-g1", files: fixture.files } };
    if (command === "read") {
      if (fixture.mode === "stale")
        throw new Error("Fixture: log was pruned; refresh the file list");
      if (!Object.prototype.hasOwnProperty.call(fixture.contents, data.id))
        throw new Error("Log not found");
      const bytes = new TextEncoder().encode(fixture.contents[data.id]);
      let offset = data.tail
        ? Math.max(0, bytes.length - data.limit)
        : Math.min(bytes.length, data.offset || 0);
      while (offset < bytes.length && (bytes[offset] & 0xc0) === 0x80) ++offset;
      let end = Math.min(bytes.length, offset + data.limit);
      while (end > offset && end < bytes.length && (bytes[end] & 0xc0) === 0x80)
        --end;
      return {
        data: {
          id: data.id,
          generation: "fixture-g1",
          offset,
          end,
          size: bytes.length,
          text: new TextDecoder().decode(bytes.subarray(offset, end)),
          eof: end === bytes.length,
        },
      };
    }
    throw new Error(`Unsupported fixture request: ${command}`);
  },
  getInfo(target: string) {
    return api.request(target, "info-get");
  },
  getState(target: string) {
    return api.request(target, "state-get");
  },
  getConfig(target: string) {
    return api.request(target, "config-get");
  },
  setConfig(target: string, data: Record<string, any>) {
    return api.request(target, "config-set", data);
  },
} as unknown as IBackendApi;

Object.assign(window, {
  logsFixture: { fixture, requests, subscriptions, pushState },
});

void i18next
  .use(initReactI18next)
  .init({
    lng: query.get("lang") || "en",
    fallbackLng: "en",
    resources: {
      en: { translation: {} },
      ...Object.fromEntries(
        Object.entries(i18n.resources).map(([language, translation]) => [
          language,
          { translation },
        ]),
      ),
    },
    interpolation: { escapeValue: false },
  })
  .then(() => {
    createRoot(document.getElementById("root")!).render(
      <Provider store={store}>
        <ClientContext.Provider value={api}>
          <ThemeProvider theme={createTheme()}>
            <CssBaseline />
            <Typography variant="h4" sx={{ m: 2 }}>
              System
            </Typography>
            <Alert severity="info" sx={{ mx: 2 }}>
              Local visual fixture · no device connection · test data
            </Alert>
            <Content />
          </ThemeProvider>
        </ClientContext.Provider>
      </Provider>,
    );
  });
