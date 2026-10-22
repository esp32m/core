import { useCallback, useEffect, useRef, useState } from 'react';
import { errorToString } from '@ts-libs/tools';
import {
  ConnectionStatus,
  useBackendApi,
  useSubscribedState,
} from '../../backend';
import { discoverLogs } from './api';
import { LogConfig, LogInfo, LogState, Name } from './types';

export function useLogs() {
  const api = useBackendApi();
  const [connected, setConnected] = useState(
    api.status.is(ConnectionStatus.Connected)
  );
  const [supported, setSupported] = useState<boolean>();
  const [info, setInfo] = useState<LogInfo>();
  const [config, setConfig] = useState<LogConfig>();
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const epoch = useRef(0);
  const watchState = connected && supported === true;
  const [snapshot, , subscribed, stateError] = useSubscribedState<LogState>(
    Name,
    {
      condition: watchState,
    }
  );
  const live = watchState && subscribed;
  // The shared cache retains the last connection's value while reconnecting.
  // Only the current subscription may make that value actionable again.
  const state = live ? snapshot : undefined;

  const reloadMetadata = useCallback(async () => {
    if (!api.status.is(ConnectionStatus.Connected)) return false;
    const current = ++epoch.current;
    setLoading(true);
    setError('');
    try {
      const capability = await discoverLogs(api);
      if (current !== epoch.current) return false;
      setSupported(!!capability);
      setInfo(capability || undefined);
      if (!capability) return false;
      const settings = await api.getConfig(Name);
      if (current !== epoch.current) return false;
      setConfig(settings.data);
      return true;
    } catch (e) {
      if (current === epoch.current)
        setError(errorToString(e) || 'Could not read log settings');
      return false;
    } finally {
      if (current === epoch.current) setLoading(false);
    }
  }, [api]);

  useEffect(() => {
    const update = () => {
      const online = api.status.is(ConnectionStatus.Connected);
      setConnected(online);
      ++epoch.current;
      setLoading(false);
      setSupported(undefined);
      setInfo(undefined);
      setConfig(undefined);
      if (online) void reloadMetadata();
    };
    const subscription = api.status.changed.subscribe(update);
    update();
    return () => {
      ++epoch.current;
      subscription.unsubscribe();
    };
  }, [api, reloadMetadata]);

  return {
    api,
    connected,
    supported,
    info,
    state,
    live,
    config,
    loading: loading || (watchState && !live && !stateError),
    error: error || (watchState ? stateError || '' : ''),
    reloadMetadata,
  };
}
