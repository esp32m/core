import { TReduxPlugin } from '@ts-libs/redux';
import { Middleware } from 'redux';
import { Name, TBackendStateRoot, actions, reducer } from './state';
import { actions as publicActions } from './types';
import { TAppLoadingPlugin, TUiRootPlugin } from '@ts-libs/ui-base';
import { Hoc } from './hoc';
import { ConnectionStatus } from './types';
import { client, isBroadcast, isResponse } from './client';
import { TErrorDeserializerPlugin } from '@ts-libs/tools';
import { EspError } from './errors';
import { Ti18nPlugin } from '@ts-libs/ui-i18n';
import { i18n } from './i18n';

export const pluginUiBackend = (): TReduxPlugin &
  TAppLoadingPlugin<TBackendStateRoot> &
  TUiRootPlugin &
  TErrorDeserializerPlugin &
  Ti18nPlugin => {
  const middleware: Middleware = (api) => {
    const c = client();
    c.status.changed.subscribe(() => {
      api.dispatch(actions.status(c.status.value));
    });
    c.incoming.subscribe((msg) => {
      if (isBroadcast(msg)) api.dispatch(publicActions.broadcast(msg));
      else if (isResponse(msg) && !msg.error)
        if (msg.name == 'state-get')
          api.dispatch(actions.deviceState([msg.source, msg.data]));
    });
    return (next) => (action) => next(action);
  };
  return {
    name: Name,
    redux: {
      reducer,
      middleware,
    },
    errors: {
      deserializer: EspError.deserialize,
    },
    i18n,
    appLoadingSelector: (state: TBackendStateRoot) =>
      state.backend.status != ConnectionStatus.Connected,
    ui: {
      root: {
        hoc: Hoc,
      },
    },
  };
};
