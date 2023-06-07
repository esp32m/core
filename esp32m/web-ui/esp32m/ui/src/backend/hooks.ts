import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useState,
} from 'react';
import { IBackendApi } from './types';
import { useDispatch, useSelector } from 'react-redux';
import { TSelector, TStateRoot } from '@ts-libs/redux';
import { isNumber, noop } from '@ts-libs/tools';
import { Name } from './state';
import { AnyAction } from 'redux';
import { ActionCreator } from 'redux';

export const ClientContext = createContext<IBackendApi | undefined>(undefined);

export const useBackendApi = () => {
  const ctx = useContext(ClientContext);
  if (!ctx) throw new Error('no backend client context');
  return ctx;
};

type TBaseRequestOptions = {
  condition?: boolean;
  data?: any;
};

type TRequestOptions = TBaseRequestOptions & {
  delay?: number;
  interval?: number;
};

class Pending<T> {
  readonly promise: Promise<T>;
  get resolve() {
    return this._resolve || noop;
  }
  get reject() {
    return this._reject || noop;
  }
  constructor() {
    this.promise = new Promise<T>((resolve, reject) => {
      this._resolve = resolve;
      this._reject = reject;
    });
  }
  private _resolve?: (value: T | PromiseLike<T>) => void;
  private _reject?: (reason: any) => void;
}

export function useRequest<T = unknown>(
  target: string,
  name: string,
  options?: TRequestOptions
) {
  const [refreshPending, setRefreshPending] = useState<
    Pending<T> | undefined
  >();
  const [running, setRunning] = useState(false);
  const [result, setResult] = useState<T>();
  const { condition, data, delay, interval } = options || {};
  const refresh = useCallback(() => {
    let pending = refreshPending;
    if (!pending) setRefreshPending((pending = new Pending<T>()));
    return pending.promise;
  }, [refreshPending]);
  const api = useBackendApi();
  const doit = useCallback(async () => {
    if (refreshPending || condition === undefined || condition)
      try {
        setRunning(true);
        const response = await api.request(target, name, data);
        refreshPending?.resolve(response.data);
        setResult(response.data);
      } catch (e) {
        refreshPending?.reject(e);
        console.error(e);
      } finally {
        setRefreshPending(undefined);
        setRunning(false);
      }
  }, [api, data, name, target, condition, refreshPending]);
  useEffect(() => {
    let th: ReturnType<typeof setTimeout>;
    let ti: ReturnType<typeof setInterval>;
    let done = false;
    const run = isNumber(interval)
      ? async () => {
          await doit();
          if (!done) ti = setTimeout(run, interval);
        }
      : doit;
    if (isNumber(delay)) th = setTimeout(run, delay);
    else run();
    return () => {
      done = true;
      clearTimeout(th);
      clearInterval(ti);
    };
  }, [delay, interval, doit]);
  return [result, refresh, running] as const;
}

type TCachedRequestOptions<T = unknown> = TRequestOptions & {
  action: ActionCreator<AnyAction>;
  selector: TSelector<T>;
};

export function useCachedRequest<T = unknown>(
  target: string,
  name: string,
  options: TCachedRequestOptions<T>
) {
  const { action, selector, ...rest } = options;
  const selected = useSelector(selector);
  const dispatch = useDispatch();
  const condition = selected == null;
  const [result, update, running] = useRequest<T>(target, name, {
    ...rest,
    condition,
  });
  useEffect(() => {
    if (condition) dispatch(action(result));
  }, [condition, action, result, dispatch]);
  return [condition ? result : selected, update, running] as const;
}

type TGetStateOptions = TRequestOptions & {
  once?: boolean;
};

export const useModuleState = <T>(
  name: string,
  options?: TGetStateOptions
): T | undefined => {
  const api = useBackendApi();
  const device = api.module(name);
  const { condition, once, data } = options || {};
  useEffect(() => {
    if (once) api.getState(name, data);
    else if (condition === undefined || condition) return device.useState(data);
  }, [api, device, name, condition, once, data]);
  return useSelector((state: TStateRoot) => device.selectors.state(state));
};

export function useModuleConfig<T = unknown>(
  target: string,
  options?: TRequestOptions
) {
  return useRequest<T>(target, 'config-get', options);
  /* const [gen, setGen] = useState(0);
  const [config, setConfig] = useState<T>();
  const api = useBackendApi();
  useEffect(() => {
    api.getConfig(target).then(
      (response) => setConfig(response.data),
      (e) => console.error(e)
    );
  }, [api, target, gen]);
  return [config, () => setGen(gen + 1)];*/
}

export const moduleStateSelector =
  <T>(name: string) =>
  (state: TStateRoot) =>
    state[Name].modules[name]?.state as T;
