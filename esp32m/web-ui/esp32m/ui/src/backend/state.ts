import { PayloadAction, createSlice } from '@reduxjs/toolkit';
import { ConnectionStatus } from './types';
import { TStateRoot } from '@ts-libs/redux';

export const Name = 'backend';

type TDeviceState = {
  state: any;
};

type TBackendState = {
  status: ConnectionStatus;
  error?: unknown;
  devices: Record<string, TDeviceState>;
};

declare module '@ts-libs/redux' {
  export interface TStateRoot {
    [Name]: TBackendState;
  }
}

const initialState: TBackendState = {
  status: ConnectionStatus.Disconnected,
  devices: {},
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
      { payload: [name, deviceState] }: PayloadAction<[string, unknown]>
    ) => {
      const ds = state.devices[name] || (state.devices[name] = { state: {} });
      ds.state = deviceState;
    },
  },
});

export const selectors = {
  devices: (state: TStateRoot) => state[Name].devices,
};

export const { actions, reducer } = slice;
