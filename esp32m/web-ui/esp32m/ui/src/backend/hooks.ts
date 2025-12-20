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
import { useSnackApi } from '@ts-libs/ui-snack';
import { isNumber, noop, TJsonValue } from '@ts-libs/tools';
import { Name, TBackendStateRoot } from './state';
import { ActionCreator, UnknownAction } from 'redux';
import { setIn } from 'formik';

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
  action: ActionCreator<UnknownAction>;
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

export const useModuleState = <T extends TJsonValue>(
  name: string,
  options?: TGetStateOptions
): T | undefined => {
  const api = useBackendApi();
  const device = api.module(name);
  const { condition, once, data } = options || {};
  useEffect(() => {
    if (once) api.getState(name, data);
    else if (condition === undefined || condition)
      return device.useState(data);
  }, [api, device, name, condition, once, data]);
  return useSelector((state: TBackendStateRoot) =>
    device.selectors.state(state)
  ) as T;
};

export const useModuleApi = (target: string) => {
  const api = useBackendApi();
  const snack = useSnackApi();
  const [inProgress, setInProgress] = useState(0);
  const enter = () => setInProgress((v) => v + 1);
  const leave = () => setInProgress((v) => v - 1);
const request = useCallback(async <T extends TJsonValue>(name: string, data?: T) => {
    try {
      enter()
      return await api.request(target, name, data);
    } catch (e) {
      snack.error(e);
    } finally {
      leave();
    }
  }, [target, api, snack]);
  const getState = useCallback(async <T extends TJsonValue>(data?: T) => {
    try {
      enter();
      return await api.getState(target, data);
    } catch (e) {
      snack.error(e);
    } finally {
      leave();
    }
  }, [target, api, snack]);
  const setState = useCallback(async <T extends TJsonValue>(data: T) => {
    try {
      enter();
      const response = await api.setState(target, data);
      return response?.data
    } catch (e) {
      snack.error(e);
    } finally {
      leave();
    }
  }, [target, api, snack]);
  const getConfig = useCallback(async <T extends TJsonValue>(data?: T) => {
    try {
      enter();
      const response = await api.getConfig(target, data);
      return response?.data
    } catch (e) {
      snack.error(e);
    } finally {
      leave();
    }
  }, [target, api, snack]);
  const setConfig = useCallback(async <T extends TJsonValue>(data: T) => {
    try {
      enter();
      const response = await api.setConfig(target, data);
      return response?.data
    } catch (e) {
      snack.error(e);
    } finally {
      leave();
    }
  }, [target, api, snack]);
  const getInfo = useCallback(async <T extends TJsonValue>(data?: T) => {
    try {
      enter();
      const response = await api.getInfo(target, data);
      return response?.data
    } catch (e) {
      snack.error(e);
    } finally {
      leave();
    }
  }, [target, api, snack]);

  return { request, getState, setState, getConfig, setConfig, getInfo, inProgress } as const;
};

export function useModuleConfig<T = unknown>(
  target: string,
  options?: TRequestOptions
) {
  const api = useModuleApi(target);
  const { setConfig, inProgress: settingConfig } = api;
  const [config, refreshConfig] = useRequest<T>(target, 'config-get', options);
  return { config, setConfig, refreshConfig, settingConfig } as const;
  //  return useRequest<T>(target, 'config-get', options);
}

export function useModuleInfo<T = unknown>(
  target: string,
  options?: TRequestOptions
) {
  return useRequest<T>(target, 'info-get', options);
}

export const moduleStateSelector =
  <T>(name: string) =>
    (state: TStateRoot) =>
      state[Name].modules[name]?.state as T;
