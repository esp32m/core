import {
  AnyAction,
  Middleware,
  Reducer,
  Selector,
  Store,
  ThunkDispatch,
} from '@reduxjs/toolkit';
import { TPlugin } from '@ts-libs/plugins';
import { PersistConfig, Persistor } from 'redux-persist';
import { Observable } from 'rxjs';

export type TReduxPlugin = TPlugin & {
  redux: {
    reducer?: Reducer;
    middleware?: Middleware;
    persist?: boolean | PersistConfig<any>;
    storage?: Storage;
  };
};

export interface TStateRoot {
  [key: string]: any;
}

export type AsyncThunkAction<S = TStateRoot> = (
  dispatch: ThunkDispatch<S, unknown, AnyAction>,
  getState: () => S
) => Promise<any>;

export type ThunkAction<S = TStateRoot> = (
  dispatch: ThunkDispatch<S, unknown, AnyAction>,
  getState: () => S
) => any;

export type TSelector<
  R = unknown,
  S = TStateRoot,
  P extends never | readonly any[] = any[]
> = Selector<S, R, P>;

export type TObservableSelectorOptions<S = TStateRoot> = {
  store?: Store<S>;
  emitInitial?: boolean;
  shallow?: boolean;
  debounce?: number;
};

export interface IRedux<S = TStateRoot> {
  readonly store: Store<S>;
  readonly persistor?: Promise<Persistor | undefined>;
  readonly dispatch: ThunkDispatch<S, unknown, AnyAction>;
  getState(): S;
  observable(debounce?: number): Observable<S>;
  observableSelector<T>(
    selector: Selector<S, T>,
    options?: TObservableSelectorOptions<S>
  ): Observable<[T, T | undefined]>;
}
