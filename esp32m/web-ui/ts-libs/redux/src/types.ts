import {
  Middleware,
  Reducer,
  Selector,
  Store,
  ThunkDispatch,
  UnknownAction,
} from '@reduxjs/toolkit';
import { TPlugin } from '@ts-libs/plugins';
import { PersistConfig, Persistor } from 'redux-persist';
import { Observable } from 'rxjs';

export type TReducer = {
  readonly name: string;
  readonly reducer: Reducer;
  readonly persist?: boolean | PersistConfig<any>;
};

export type TReduxPlugin = TPlugin & {
  readonly redux: {
    readonly reducers?: ReadonlyArray<TReducer>;
    readonly middleware?: Middleware;
    readonly storage?: Storage;
  } & Partial<TReducer>;
};

export interface TStateRoot {
  [key: string]: any;
}

export type AsyncThunkAction<S = TStateRoot> = (
  dispatch: ThunkDispatch<S, unknown, UnknownAction>,
  getState: () => S
) => Promise<any>;

export type ThunkAction<S = TStateRoot> = (
  dispatch: ThunkDispatch<S, unknown, UnknownAction>,
  getState: () => S
) => any;

export type TSelector<
  R = unknown,
  S = TStateRoot,
  P extends never | readonly any[] = any[],
> = Selector<S, R, P>;

export type TObservableSelectorOptions<S = TStateRoot> = {
  readonly store?: Store<S>;
  readonly emitInitial?: boolean;
  readonly shallow?: boolean;
  readonly debounce?: number;
};

export interface IRedux<S extends TStateRoot = TStateRoot> {
  readonly store: Store<S>;
  readonly persistor?: Promise<Persistor | undefined>;
  readonly dispatch: ThunkDispatch<S, unknown, UnknownAction>;
  getState(): S;
  observable(debounce?: number): Observable<S>;
  observableSelector<T>(
    selector: Selector<S, T>,
    options?: TObservableSelectorOptions<S>
  ): Observable<[T, T | undefined]>;
}
