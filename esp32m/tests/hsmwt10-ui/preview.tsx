import { createRoot } from 'react-dom/client';
import { Provider } from 'react-redux';
import { createStore } from 'redux';
import { CssBaseline, Grid, ThemeProvider, createTheme } from '@mui/material';
import i18next from 'i18next';
import { initReactI18next } from 'react-i18next';
import { ClientContext } from '../../web-ui/esp32m/ui/src/backend/hooks';
import { IBackendApi } from '../../web-ui/esp32m/ui/src/backend/types';
import { Content } from '../../web-ui/esp32m/ui/src/dev/hsmwt10/Content';

type TState = {
  address: number;
  baud: number;
  valid: boolean;
  age: number;
  alarm: boolean | null;
  analog: number | null;
  communicationPending: boolean;
  lastError: number;
};
type TConfig = {
  address: number;
  baud: number;
  sensitivity?: number;
  alarmDelay?: number;
  configReadError: number;
  pendingAddress?: number;
  pendingBaud?: number;
};
type TCall = { target: string; command: string; data?: unknown };
const calls: TCall[] = [];
let config: TConfig = {
  address: 5,
  baud: 9600,
  sensitivity: 300,
  alarmDelay: 0,
  configReadError: 0,
};
let writeDelay = 0;
let activeScenario = 'normal';
let publishing = false;
const normal = (): TState => ({
  address: config.address,
  baud: config.baud,
  valid: true,
  age: 100,
  alarm: false,
  analog: 723,
  communicationPending: false,
  lastError: 0,
});
const store = createStore(
  (state = normal(), action: { type: string; state?: TState }) =>
    action.type === 'fixture' ? action.state! : state,
);
const module = {
  selectors: { state: (state: TState) => state },
  useState: () => () => undefined,
};
const dispatch = (state: TState) => store.dispatch({ type: 'fixture', state });
const fixture = {
  calls,
  getState: () => store.getState(),
  getConfig: () => structuredClone(config),
  startTelemetry: () => {
    publishing = true;
    dispatch({ ...store.getState(), age: 100 });
  },
  stopTelemetry: () => {
    publishing = false;
  },
  setWriteDelay: (milliseconds: number) => {
    writeDelay = milliseconds;
  },
  setConfigReadError: (error: number) => {
    config = { ...config, configReadError: error };
    if (error) {
      delete config.sensitivity;
      delete config.alarmDelay;
    } else {
      config.sensitivity = 300;
      config.alarmDelay = 0;
    }
  },
  setScenario: (scenario: string) => {
    activeScenario = scenario;
    delete config.pendingAddress;
    delete config.pendingBaud;
    const state = normal();
    switch (scenario) {
      case 'normal':
        break;
      case 'leak':
        state.alarm = true;
        state.analog = 180;
        break;
      case 'stale':
        state.age = 6000;
        // Exercise age validation even if a formerly valid value is present.
        break;
      case 'offline':
        state.valid = false;
        state.age = -1;
        state.alarm = null;
        state.analog = null;
        state.lastError = 263;
        break;
      case 'pending':
        state.valid = false;
        state.alarm = null;
        state.analog = null;
        state.communicationPending = true;
        state.lastError = 259;
        config.pendingAddress = 7;
        config.pendingBaud = 19200;
        break;
      default:
        throw new Error(`Unknown fixture scenario: ${scenario}`);
    }
    dispatch(state);
  },
};
(window as unknown as { hsmwt10Test: typeof fixture }).hsmwt10Test = fixture;
// Simulate normal polling; stale/offline/pending scenarios deliberately stop it.
setInterval(() => {
  if (publishing && (activeScenario === 'normal' || activeScenario === 'leak'))
    dispatch({ ...store.getState(), age: 100 });
}, 1000);

const api = {
  module: () => module,
  getState: async (target: string) => {
    calls.push({ target, command: 'state-get' });
    return { data: store.getState() };
  },
  request: async (
    target: string,
    command: string,
    data: Record<string, number>,
  ) => {
    calls.push({ target, command, data: structuredClone(data) });
    if (target !== 'HSM-WT10')
      throw new Error(`Unexpected fixture target: ${target}`);
    if (command === 'config-get') return { data: structuredClone(config) };
    if (command === 'config-set') {
      if (writeDelay)
        await new Promise((resolve) => setTimeout(resolve, writeDelay));
      config = { ...config, ...data };
      dispatch({
        ...store.getState(),
        address: config.address,
        baud: config.baud,
      });
      return { data: structuredClone(config) };
    }
    throw new Error(`Unsupported fixture request: ${command}`);
  },
} as unknown as IBackendApi;

void i18next
  .use(initReactI18next)
  .init({
    lng: 'en',
    fallbackLng: 'en',
    resources: { en: { translation: {} } },
    interpolation: { escapeValue: false },
  })
  .then(() => {
    createRoot(document.getElementById('root')!).render(
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
