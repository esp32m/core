import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useState,
} from 'react';
import { IBackendApi } from './types';
import { useSelector } from 'react-redux';
import { TStateRoot } from '@ts-libs/redux';
import { isNumber } from '@ts-libs/tools';

export const ClientContext = createContext<IBackendApi | undefined>(undefined);

export const useBackendApi = () => {
  const ctx = useContext(ClientContext);
  if (!ctx) throw new Error('no backend client context');
  return ctx;
};

type TRequestOptions = {
  condition?: boolean;
  data?: any;
  delay?: number;
  interval?: number;
};

export function useRequest<T = unknown>(
  target: string,
  name: string,
  options?: TRequestOptions
): [T | undefined, () => void, boolean] {
  const [gen, setGen] = useState(0);
  const [running, setRunning] = useState(false);
  const [result, setResult] = useState<T>();
  const { condition, data, delay, interval } = options || {};
  const update = useCallback(() => setGen(gen + 1), [gen]);
  const api = useBackendApi();
  const doit = useCallback(async () => {
    if (condition === undefined || condition)
      try {
        setRunning(true);
        const response = await api.request(target, name, data);
        setResult(response.data);
      } catch (e) {
        console.error(e);
      } finally {
        setRunning(false);
      }
  }, [api, data, name, target, condition]);
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
    if (isNumber(delay) && gen == 0) th = setTimeout(run, delay);
    else run();
    return () => {
      done = true;
      clearTimeout(th);
      clearInterval(ti);
    };
  }, [gen, delay, interval, doit]);
  return [result, update, running];
}

type TGetStateOptions = TRequestOptions & {
  once?: boolean;
};

export const useModuleState = <T>(
  name: string,
  options?: TGetStateOptions
): T | undefined => {
  const api = useBackendApi();
  const device = api.device(name);
  const { condition, once, data } = options || {};
  useEffect(() => {
    if (once) api.getState(name, data);
    else if (condition === undefined || condition) return device.useState();
  }, [api, device, name, condition, once, data]);
  return useSelector((state: TStateRoot) => device.selectors.state(state));
};

export function useModuleConfig<T = unknown>(
  name: string
): [T | undefined, () => void] {
  const [gen, setGen] = useState(0);
  const [config, setConfig] = useState<T>();
  const api = useBackendApi();
  useEffect(() => {
    api.getConfig(name).then(
      (response) => setConfig(response.data),
      (e) => console.error(e)
    );
  }, [api, name, gen]);
  return [config, () => setGen(gen + 1)];
}
