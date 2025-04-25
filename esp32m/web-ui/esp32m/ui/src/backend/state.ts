import { PayloadAction, createSlice } from '@reduxjs/toolkit';
import { ConnectionStatus, actions as publicActions } from './types';
import { TStateRoot } from '@ts-libs/redux';
import { TJsonValue } from '@ts-libs/tools';

export const Name = 'backend';

type TModuleState = {
  info?: TJsonValue;
  state?: TJsonValue;
  config?: TJsonValue
  activeRequests?: Record<string, number>;
};

type TBackendState = {
  status: ConnectionStatus;
  error?: unknown;
  modules: Record<string, TModuleState>;
};

export type TBackendStateRoot = TStateRoot & {
  [Name]: TBackendState;
};

const initialState: TBackendState = {
  status: ConnectionStatus.Disconnected,
  modules: {},
};

const slice = createSlice({
  name: Name,
  initialState,
  reducers: {
    status: (state, { payload }: PayloadAction<ConnectionStatus>) => {
      state.status = payload;
      if (payload == ConnectionStatus.Connected) delete state.error;
    },
    error: (state, { payload }: PayloadAction<unknown>) => {
      state.error = payload;
    },
    moduleState: (
      state,
      { payload: [name, moduleState] }: PayloadAction<[string, TJsonValue]>
    ) => {
      const ds = state.modules[name] ??= {};
      ds.state = moduleState;
    },
    moduleConfig: (
      state,
      { payload: [name, config] }: PayloadAction<[string, TJsonValue]>
    ) => {
      const ds = state.modules[name] ??= {};
      ds.config = config;
    },
    moduleInfo: (
      state,
      { payload: [name, info] }: PayloadAction<[string, TJsonValue]>
    ) => {
      const ds = state.modules[name] ??= {};
      ds.info = info;
    },
  },
  extraReducers: (builder) => {
    builder.addCase(publicActions.request, (state, { payload }) => {
      const { target, name } = payload;
      if (!target) return;
      const ms = state.modules[target] ??= {};
      const ar = ms.activeRequests ??= {};
      ar[name] ??= 0;
      ar[name]++;
    }).addCase(publicActions.requestResolved, (state, { payload: [request, response] }) => {
      const { target, name } = request;
      if (!target) return;
      const ar = state.modules[target]?.activeRequests;
      if (ar?.[name])
        ar[name]--;
    }).addCase(publicActions.requestRejected, (state, { payload: [request, error] }) => {
      const { target, name } = request;
      if (!target) return;
      const ar = state.modules[target]?.activeRequests;
      if (ar?.[name])
        ar[name]--;
    });
  }
});

export const selectors = {
  modules: (state: TBackendStateRoot) => state[Name].modules,
};

export const { actions, reducer } = slice;
