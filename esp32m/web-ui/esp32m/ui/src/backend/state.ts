import { PayloadAction, createSlice } from '@reduxjs/toolkit';
import type { Draft } from '@reduxjs/toolkit';
import {
  ConnectionStatus,
  actions as publicActions,
  TStateUpdate,
} from './types';
import { TStateRoot } from '@ts-libs/redux';
import { TJsonValue } from '@ts-libs/tools';

export const Name = 'backend';

type TModuleState = {
  info?: TJsonValue;
  state?: TJsonValue;
  config?: TJsonValue;
  activeRequests?: Record<string, number>;
  views?: Record<
    string,
    {
      state?: TJsonValue;
      live: boolean;
      stream?: string;
      rev?: number;
      error?: string;
      epoch?: number;
    }
  >;
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
    status: (
      state: Draft<TBackendState>,
      { payload }: PayloadAction<ConnectionStatus>
    ) => {
      state.status = payload;
      if (payload == ConnectionStatus.Connected) delete state.error;
    },
    error: (state: Draft<TBackendState>, { payload }: PayloadAction<unknown>) => {
      state.error = payload;
    },
    stateUpdate: (
      state: Draft<TBackendState>,
      { payload }: PayloadAction<TStateUpdate>
    ) => {
      const { target, key, state: value, live, stream, rev, error } = payload;
      const module = (state.modules[target] ??= {});
      const views = (module.views ??= {});
      const cached = (views[key] ??= { live: false });
      cached.live = live;
      cached.stream = stream;
      cached.rev = rev;
      cached.error = error;
      if (payload.epoch !== undefined) cached.epoch = payload.epoch;
      if (value !== undefined) {
        (cached as any).state = value;
        if (key === 'state') (module as any).state = value;
      }
    },
    moduleState: (
      state: Draft<TBackendState>,
      { payload: [name, moduleState] }: PayloadAction<[string, TJsonValue]>
    ) => {
      const ds = (state.modules[name] ??= {});
      (ds as any).state = moduleState;
    },
    moduleConfig: (
      state: Draft<TBackendState>,
      { payload: [name, config] }: PayloadAction<[string, TJsonValue]>
    ) => {
      const ds = (state.modules[name] ??= {});
      (ds as any).config = config;
    },
    moduleInfo: (
      state: Draft<TBackendState>,
      { payload: [name, info] }: PayloadAction<[string, TJsonValue]>
    ) => {
      const ds = (state.modules[name] ??= {});
      (ds as any).info = info;
    },
  },
  extraReducers: (builder) => {
    builder
      .addCase(publicActions.request, (state, { payload }) => {
        const { target, name } = payload;
        if (!target) return;
        const ms = (state.modules[target] ??= {});
        const ar = (ms.activeRequests ??= {});
        ar[name] ??= 0;
        ar[name]++;
      })
      .addCase(
        publicActions.requestResolved,
        (state, { payload: [request, response] }) => {
          const { target, name } = request;
          if (!target) return;
          const ar = state.modules[target]?.activeRequests;
          if (ar?.[name]) ar[name]--;
        }
      )
      .addCase(
        publicActions.requestRejected,
        (state, { payload: [request, error] }) => {
          const { target, name } = request;
          if (!target) return;
          const ar = state.modules[target]?.activeRequests;
          if (ar?.[name]) ar[name]--;
        }
      );
  },
});

export const selectors = {
  modules: (state: TBackendStateRoot) => state[Name].modules,
};

export const { actions, reducer } = slice;
