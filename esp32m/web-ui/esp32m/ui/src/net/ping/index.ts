import content from './Content';
import { INettoolsPlugin, Nettools } from '..';
import { IReduxPlugin } from '../../app';
import { ILocalState, StartAction, Name } from './types';
import { AnyAction } from 'redux';

const reducer = (
  state: ILocalState = {},
  { type, payload = {} }: AnyAction
) => {
  if (type == StartAction) {
    return {};
  } else if (payload.source == Name)
    if (type == 'backend/response' && payload.data)
      switch (payload.name) {
        case 'start': {
          const { results = [] } = state;
          return { results: [...results, payload.data] };
        }
      }
  return state;
};

export const Ping: INettoolsPlugin & IReduxPlugin = {
  name: 'ping',
  use: Nettools,
  reducer,
  nettools: {
    content,
  },
};
