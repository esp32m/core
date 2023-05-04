import content from './Content';
import { INettoolsPlugin, Nettools } from '..';
import { ILocalState, StartAction, Name } from './types';
import { AnyAction } from 'redux';
import { TReduxPlugin } from '@ts-libs/redux';

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

export const Ping: INettoolsPlugin & TReduxPlugin = {
  name: 'ping',
  use: Nettools,
  redux: { reducer },
  nettools: {
    content,
  },
};
