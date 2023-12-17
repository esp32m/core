import { PayloadAction, createSlice } from '@reduxjs/toolkit';
import { ConnectionStatus } from './types';
import { TStateRoot } from '@ts-libs/redux';

export const Name = 'backend';

type TModuleState = {
  state: any;
};

type TBackendState = {
  status: ConnectionStatus;
  error?: unknown;
  modules: Record<string, TModuleState>;
};

export type TBackendStateRoot = TStateRoot & {
  [Name]: TBackendState;
};

/*declare module '@ts-libs/redux' {
  export interface TStateRoot {
    [Name]: TBackendState;
  }
}*/

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
    deviceState: (
      state,
      { payload: [name, moduleState] }: PayloadAction<[string, unknown]>
    ) => {
      const ds = state.modules[name] || (state.modules[name] = { state: {} });
      ds.state = moduleState;
    },
  },
});

export const selectors = {
  modules: (state: TBackendStateRoot) => state[Name].modules,
};

export const { actions, reducer } = slice;
