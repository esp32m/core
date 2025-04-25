import { createSelector, createSlice } from '@reduxjs/toolkit';
import { actions, moduleStateSelector } from '../../backend';
import { TStateRoot } from '@ts-libs/redux';
import { TOtaState } from './types';

export const Name = 'ota';

type TLocalOtaState = {
  running?: boolean;
};

declare module '@ts-libs/redux' {
  export interface TStateRoot {
    [Name]: TLocalOtaState;
  }
}

const initialState: TLocalOtaState = {};

const slice = createSlice({
  name: Name,
  initialState,
  extraReducers: (builder) => {
    builder.addCase(actions.broadcast, (state, { payload }) => {
      if (payload.source == 'ota')
        switch (payload.name) {
          case 'begin':
            state.running = true;
            break;
          case 'end':
            state.running = false;
            break;
        }
    });
  },
  reducers: {},
});

export const { reducer } = slice;
export const selectors = {
  state: createSelector(moduleStateSelector<TOtaState>(Name), (s: TStateRoot) => s[Name], (mss, ls) => {
    return {
      stateLoaded: !!mss,
      isRunning: ls.running === true || !!mss?.total,
      isFinished: ls.running === false,
    };
  }),
};
