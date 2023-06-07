import { TReduxPlugin, TStateRoot } from '@ts-libs/redux';
import { Action, Dispatch, Middleware } from 'redux';
import { Name, actions, reducer } from './state';
import { actions as publicActions } from './types';
import { TAppLoadingPlugin, TUiRootPlugin } from '@ts-libs/ui-base';
import { Hoc } from './hoc';
import { ConnectionStatus } from './types';
import { client, isBroadcast, isResponse } from './client';

export const pluginUiBackend = (): TReduxPlugin &
  TAppLoadingPlugin &
  TUiRootPlugin => {
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
    return (next: Dispatch) => (action: Action) => next(action);
  };
  return {
    name: Name,
    redux: {
      reducer,
      middleware,
    },
    appLoadingSelector: (state: TStateRoot) =>
      state.backend.status != ConnectionStatus.Connected,
    ui: {
      root: {
        hoc: Hoc,
      },
    },
  };
};
