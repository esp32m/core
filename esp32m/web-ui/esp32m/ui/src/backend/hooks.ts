import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useSyncExternalStore,
  useState,
} from 'react';
import { IBackendApi, TRequestOptions, TStateView } from './types';
import { useDispatch, useSelector } from 'react-redux';
import { TSelector, TStateRoot } from '@ts-libs/redux';
import { useSnackApi } from '@ts-libs/ui-snack';
import { isNumber, noop, TJsonValue } from '@ts-libs/tools';
import { Name } from './state';
import { ActionCreator, UnknownAction } from 'redux';
import { canonicalQuery } from './subscriptions';
import { sampleAge } from './clock';

export const ClientContext = createContext<IBackendApi | undefined>(undefined);

export const useBackendApi = () => {
  const ctx = useContext(ClientContext);
  if (!ctx) throw new Error('no backend client context');
  return ctx;
};

type TBaseRequestOptions = TRequestOptions & {
  condition?: boolean;
  data?: any;
};

type TUseRequestOptions = TBaseRequestOptions & {
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
  options?: TUseRequestOptions
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
        const response = await api.request(target, name, data, options);
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

type TCachedRequestOptions<T = unknown> = TUseRequestOptions & {
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

type TGetStateOptions = TUseRequestOptions & {
  once?: boolean;
  view?: TStateView;
};

export const useSubscribedState = <T = TJsonValue>(
  name: string,
  options?: TGetStateOptions
) => {
  const api = useBackendApi();
  const device = api.module(name);
  const { condition, once, view = 'state' } = options || {};
  const queryKey = canonicalQuery(options?.data);
  const data = useMemo(
    () => (queryKey === '' ? undefined : JSON.parse(queryKey)),
    [queryKey]
  );
  useEffect(() => {
    if (condition === false) return;
    if (once) void api.refreshState(name, data, view).catch(console.error);
    else return device.useState(data, view);
  }, [api, device, name, condition, once, data, view]);
  const selector = useMemo(
    () => device.stateSelector(data, view),
    [device, data, view]
  );
  const liveSelector = useMemo(
    () => device.liveSelector(data, view),
    [device, data, view]
  );
  const state = useSelector(selector) as T | undefined;
  const live = useSelector(liveSelector);
  const errorSelector = useMemo(
    () => device.errorSelector(data, view),
    [device, data, view]
  );
  const error = useSelector(errorSelector);
  const refresh = useCallback(
    async () => (await api.refreshState(name, data, view)).data as T,
    [api, name, data, view]
  );
  return [state, refresh, live, error] as const;
};

export const useModuleState = <T extends TJsonValue>(
  name: string,
  options?: TGetStateOptions
): T | undefined => useSubscribedState<T>(name, options)[0];

/** Exact acquisition age; a silent changed-state stream is not a failed sample. */
export const useSampleAger = (
  target: string,
  options?: { data?: any; view?: TStateView; once?: boolean }
) => {
  const api = useBackendApi();
  const module = api.module(target);
  const live = useSelector(module.liveSelector(options?.data, options?.view));
  const epoch = useSelector(module.epochSelector(options?.data, options?.view));
  useSyncExternalStore(api.clock.subscribe, api.clock.getSnapshot, api.clock.getServerSnapshot);
  const generation = api.clock.generation;
  return (sampledAtMs: number | null | undefined) =>
    generation === api.clock.generation && epoch === generation && (live || options?.once)
      ? sampleAge(sampledAtMs, api.clock.now()) : -1;
};

export const useSampleAge = (sampledAtMs: number | null | undefined, target: string,
  options?: { data?: any; view?: TStateView; once?: boolean }) => useSampleAger(target, options)(sampledAtMs);

export const useModuleApi = (target: string) => {
  const api = useBackendApi();
  const snack = useSnackApi();
  const [inProgress, setInProgress] = useState(0);
  const enter = () => setInProgress((v) => v + 1);
  const leave = () => setInProgress((v) => v - 1);
  const request = useCallback(
    async <T extends TJsonValue>(
      name: string,
      data?: T,
      options?: TRequestOptions
    ) => {
      try {
        enter();
        return await api.request(target, name, data, options);
      } catch (e) {
        snack.error(e);
      } finally {
        leave();
      }
    },
    [target, api, snack]
  );
  const getState = useCallback(
    async <T extends TJsonValue>(data?: T) => {
      try {
        enter();
        return await api.getState(target, data);
      } catch (e) {
        snack.error(e);
      } finally {
        leave();
      }
    },
    [target, api, snack]
  );
  const setState = useCallback(
    async <T extends TJsonValue>(data: T) => {
      try {
        enter();
        const response = await api.setState(target, data);
        return response?.data;
      } catch (e) {
        snack.error(e);
      } finally {
        leave();
      }
    },
    [target, api, snack]
  );
  const getConfig = useCallback(
    async <T extends TJsonValue>(data?: T) => {
      try {
        enter();
        const response = await api.getConfig(target, data);
        return response?.data;
      } catch (e) {
        snack.error(e);
      } finally {
        leave();
      }
    },
    [target, api, snack]
  );
  const setConfig = useCallback(
    async <T extends TJsonValue>(data: T) => {
      try {
        enter();
        const response = await api.setConfig(target, data);
        return response?.data;
      } catch (e) {
        snack.error(e);
      } finally {
        leave();
      }
    },
    [target, api, snack]
  );
  const getInfo = useCallback(
    async <T extends TJsonValue>(data?: T) => {
      try {
        enter();
        const response = await api.getInfo(target, data);
        return response?.data;
      } catch (e) {
        snack.error(e);
      } finally {
        leave();
      }
    },
    [target, api, snack]
  );

  return {
    request,
    getState,
    setState,
    getConfig,
    setConfig,
    getInfo,
    inProgress,
  } as const;
};

export function useModuleConfig<T = unknown>(
  target: string,
  options?: TUseRequestOptions
) {
  const api = useModuleApi(target);
  const { setConfig, inProgress: settingConfig } = api;
  const [config, refreshConfig] = useRequest<T>(target, 'config-get', options);
  return { config, setConfig, refreshConfig, settingConfig } as const;
  //  return useRequest<T>(target, 'config-get', options);
}

export function useModuleInfo<T = unknown>(
  target: string,
  options?: TUseRequestOptions
) {
  return useRequest<T>(target, 'info-get', options);
}

export const moduleStateSelector =
  <T>(name: string) =>
  (state: TStateRoot) =>
    state[Name].modules[name]?.state as T;
