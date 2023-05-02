import { Name } from './types';
import { useBackendApi } from '../../backend';
import { useCallback } from 'react';

export const useConnector = () => {
  const api = useBackendApi();
  return useCallback(
    (ssid: string, bssid?: string, password?: string) =>
      api.request(Name, 'connect', { ssid, bssid, password }),
    [api]
  );
};
