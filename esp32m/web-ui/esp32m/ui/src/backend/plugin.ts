import { TReduxPlugin } from '@ts-libs/redux';
import { Middleware } from 'redux';
import { Name, TBackendStateRoot, actions, reducer } from './state';
import { MessageName, actions as publicActions } from './types';
import { TAppLoadingPlugin, TUiRootPlugin } from '@ts-libs/ui-base';
import { Hoc } from './hoc';
import { ConnectionStatus } from './types';
import { client, isBroadcast, isResponse } from './client';
import { serializeError, TErrorDeserializerPlugin } from '@ts-libs/tools';
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
      else if (isResponse(msg)) {
        if (!msg.error) {
          switch (msg.name) {
            case MessageName.GetState:
              api.dispatch(actions.moduleState([msg.source, msg.data]));
              break;
            case MessageName.SetState:
              if (msg.data)
                api.dispatch(actions.moduleState([msg.source, msg.data]));
              break;
            case MessageName.GetConfig:
              api.dispatch(actions.moduleConfig([msg.source, msg.data]));
              break;
            case MessageName.SetConfig:
              if (msg.data)
                api.dispatch(actions.moduleConfig([msg.source, msg.data]));
              break;
            case MessageName.GetInfo:
              api.dispatch(actions.moduleInfo([msg.source, msg.data]));
              break;
          }
        }
        api.dispatch(publicActions.response(msg));
      }
    });
    c.resolved.subscribe((msg) => {
      api.dispatch(publicActions.requestResolved(msg));
    });
    c.rejected.subscribe(([request, error]) => {
      api.dispatch(publicActions.requestRejected([request, serializeError(error)]));
    });
    c.outgoing.subscribe((req) => {
      api.dispatch(publicActions.request(req));
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
