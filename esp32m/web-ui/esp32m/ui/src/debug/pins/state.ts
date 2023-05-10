import { PayloadAction, createSlice } from '@reduxjs/toolkit';
import { TStateRoot } from '@ts-libs/redux';
import { FeatureType, TPinState } from './types';

export const Name = 'debug-pins';

export type TPinNames = Record<string, Array<number>>;
export type TPinKey = [provider: string, id: number];

type TLocalState = {
  names?: TPinNames;
  pin?: TPinKey;
  feature?: FeatureType;
  pinState?: TPinState;
};

declare module '@ts-libs/redux' {
  export interface TStateRoot {
    [Name]: TLocalState;
  }
}

const initialState: TLocalState = {};

const slice = createSlice({
  name: Name,
  initialState,
  reducers: {
    names: (state, { payload }: PayloadAction<TPinNames>) => {
      state.names = payload;
    },
    pin: (state, { payload }: PayloadAction<TPinKey | undefined>) => {
      state.pin = payload;
    },
    feature: (state, { payload }: PayloadAction<FeatureType | undefined>) => {
      state.feature = payload;
    },
    pinState: (state, { payload }: PayloadAction<TPinState | undefined>) => {
      state.pinState = payload;
    },
  },
});

export const { actions, reducer } = slice;

export const selectors = {
  names: (state: TStateRoot) => state[Name].names,
  pin: (state: TStateRoot) => state[Name].pin,
  feature: (state: TStateRoot) => state[Name].feature,
  pinState: (state: TStateRoot) => state[Name].pinState,
};
