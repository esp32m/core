import { createRoot } from "react-dom/client";
import { Provider } from "react-redux";
import { createStore } from "redux";
import { CssBaseline, Grid, ThemeProvider, createTheme } from "@mui/material";
import i18next from "i18next";
import { initReactI18next } from "react-i18next";
import { ClientContext } from "../../web-ui/esp32m/ui/src/backend/hooks";
import { IBackendApi } from "../../web-ui/esp32m/ui/src/backend/types";
import { ServerClock } from "../../web-ui/esp32m/ui/src/backend/clock";
import { Content } from "../../web-ui/esp32m/ui/src/dev/bfs/Content";
import {
  TConfig,
  TFaults,
  TParameters,
  TState,
} from "../../web-ui/esp32m/ui/src/dev/bfs/types";

type TFixture = {
  state: TState;
  config: TConfig;
  parameters: TParameters;
  faults: TFaults;
};
const fixture = (window as unknown as { bfsFixture: TFixture }).bfsFixture;
const clock = new ServerClock();
clock.observe(1000000);
const store = createStore(
  (state = fixture.state, action: { type: string; state?: TState }) =>
    action.type === "fixture" ? action.state! : state,
);
const module = {
  selectors: { state: (state: TState) => state },
  useState: () => () => undefined,
  stateSelector: (_data?: unknown, view = 'state') => view === 'device' ? () => deviceState : (state: TState) => state,
  liveSelector: () => () => true,
  errorSelector: () => () => undefined,
  epochSelector: () => () => clock.generation,
};
const deviceState = { _disabled: false };
const api = {
  clock,
  module: () => module,
  getState: async () => ({ data: store.getState() }),
  refreshState: async (_name: string, _data?: unknown, view = 'state') => ({ data: view === 'device' ? deviceState : store.getState() }),
  request: async (
    _name: string,
    command: string,
    data: Record<string, number | boolean>,
  ) => {
    if (command === "config-get") return { data: fixture.config };
    if (command === "config-set") {
      fixture.config = {
        address: Number(data.address),
        baud: Number(data.baud),
      };
      store.dispatch({
        type: "fixture",
        state: { ...store.getState(), ...fixture.config },
      });
      return { data: fixture.config };
    }
    if (command === "run") {
      const state = store.getState();
      const raw = (state.status.raw! & ~1) | (data.running ? 1 : 0);
      store.dispatch({
        type: "fixture",
        state: { ...state, status: { raw, value: raw, valid: true, sampledAtMs: clock.now()! } },
      });
      return { data: { accepted: true, verified: true } };
    }
    if (command === "faults") return { data: fixture.faults };
    if (command === "parameter-cache") return { data: { ...fixture.parameters, cached: true } };
    if (command === "parameters")
      return {
        data: {
          parameters: fixture.parameters.parameters.filter(
            (p) => data.address === undefined || p.address === data.address,
          ),
        },
      };
    if (command === "parameter-set") {
      const parameter = fixture.parameters.parameters.find(
        (p) => p.address === data.address,
      )!;
      parameter.raw = Number(data.raw);
      return { data: { accepted: true, verified: true, raw: parameter.raw } };
    }
    throw new Error(`Unsupported fixture request: ${command}`);
  },
} as unknown as IBackendApi;

void i18next
  .use(initReactI18next)
  .init({
    lng: "en",
    fallbackLng: "en",
    resources: { en: { translation: {} } },
    interpolation: { escapeValue: false },
  })
  .then(() => {
    createRoot(document.getElementById("root")!).render(
      <Provider store={store}>
        <ClientContext.Provider value={api}>
          <ThemeProvider theme={createTheme()}>
            <CssBaseline />
            <Grid container>
              <Content />
            </Grid>
          </ThemeProvider>
        </ClientContext.Provider>
      </Provider>,
    );
  });
