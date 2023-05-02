import { useStore } from 'react-redux';
import { ISnackApi, ISnackItem, TSnackItem } from './types';
import { actions } from './state';
import { AnyAction, Store } from '@reduxjs/toolkit';
import { errorToString } from '@ts-libs/tools';

const itemDefaults: Partial<TSnackItem> = {
  timeout: 3000,
};
const ClosureUpdateInterval = 300;

let idCounter = 0;
class Item implements ISnackItem {
  constructor(
    readonly api: Api,
    readonly id: number,
    readonly item: TSnackItem
  ) {
    this.startTimers();
  }
  close() {
    delete this.api.items[this.id];
    this.stopTimers();
    this.api.store.dispatch(actions.remove(this.id));
  }
  suspendClosure() {
    this.stopTimers();
  }
  resumeClosure() {
    this.startTimers();
  }

  private updateClosure() {
    this.api.store.dispatch(actions.updateClosure([this.id, Date.now()]));
  }
  private startTimers() {
    const { timeout, progress, timer } = this.item;
    if (timeout) {
      const showsClosure = progress || timer;
      this._th = setTimeout(
        () => this.close(),
        timeout + (showsClosure ? ClosureUpdateInterval : 0)
      );
      if (showsClosure) {
        this.updateClosure();
        this._ih = setInterval(
          () => this.updateClosure(),
          ClosureUpdateInterval
        );
      }
    }
  }
  private stopTimers() {
    clearTimeout(this._th);
    delete this._th;
    clearInterval(this._ih);
    delete this._ih;
  }
  _th?: ReturnType<typeof setTimeout>;
  _ih?: ReturnType<typeof setInterval>;
}

class Api implements ISnackApi {
  readonly items: Record<number, Item> = {};
  constructor(readonly store: Store<unknown, AnyAction>) {}
  add(item: TSnackItem) {
    const id = ++idCounter;
    const effectiveItem = { ...itemDefaults, ...item };
    const state = { id, openedAt: Date.now(), ...effectiveItem };
    this.store.dispatch(actions.add(state));
    return (this.items[id] = new Item(this, id, effectiveItem));
  }
  error(e: Error | string): ISnackItem {
    return this.add({ message: errorToString(e), severity: 'error' });
  }
  suspendClosure() {
    this.store.dispatch(actions.suspendClosure());
    Object.values(this.items).forEach((i) => i.suspendClosure());
  }
  resumeClosure() {
    this.store.dispatch(actions.resumeClosure());
    Object.values(this.items).forEach((i) => i.resumeClosure());
  }
}

const perStoreApi = new Map<Store, Api>();

export function useSnackApi() {
  const store = useStore();
  if (!perStoreApi.has(store)) perStoreApi.set(store, new Api(store));
  return perStoreApi.get(store) as ISnackApi;
}
