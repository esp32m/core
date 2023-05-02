import { PayloadAction, createSelector, createSlice } from '@reduxjs/toolkit';
import { TSnackItem } from './types';
import { TStateRoot } from '@ts-libs/redux';

export const Name = 'ui-snack';

export type TItemState = TSnackItem & {
  readonly id: number;
  readonly openedAt: number;
  readonly closure?: {
    abs: number;
    rel: number;
  };
};

type TSnackState = {
  items: Record<number, TItemState>;
  closureSuspended: boolean;
};

declare module '@ts-libs/redux' {
  export interface TStateRoot {
    [Name]: TSnackState;
  }
}

const initialState: TSnackState = { items: {}, closureSuspended: false };

const slice = createSlice({
  name: Name,
  initialState,
  reducers: {
    add: (state, { payload }: PayloadAction<TItemState>) => {
      state.items[payload.id] = payload;
    },
    remove: (state, { payload }: PayloadAction<number>) => {
      delete state.items[payload];
    },
    updateClosure: (
      state,
      { payload: [id, now] }: PayloadAction<[number, number]>
    ) => {
      const item = state.items[id];
      if (!state.closureSuspended && item?.timeout) {
        const abs = item.timeout - (now - item.openedAt);
        item.closure = { abs, rel: abs / item.timeout };
      }
    },
    suspendClosure: (state) => {
      state.closureSuspended = true;
    },
    resumeClosure: (state) => {
      Object.values(state.items).forEach((i) => (i.openedAt = Date.now()));
      state.closureSuspended = false;
    },
  },
});

function constructSelectors() {
  const items = createSelector(
    (state: TStateRoot) => state[Name],
    (state) => Object.values(state.items).sort((a, b) => a.id - b.id)
  );
  return { items };
}

export const { actions, reducer } = slice;
export const selectors = constructSelectors();
