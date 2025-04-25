import { Content } from './Content';
import { INettoolsPlugin, Nettools } from '..';
import { TLocalState, Name, StartAction } from './types';
import { actions } from '../../backend';
import { TReduxPlugin } from '@ts-libs/redux';
import { createReducer } from '@reduxjs/toolkit';


const reducer = createReducer({} as TLocalState, (builder) => {
  builder
    .addCase(StartAction, (state, action) => {
      state = {};
    })
    .addCase(actions.requestResolved, (state, action) => {
      const response=action.payload[1];
      if (response.source == Name && response.data)
        switch (response.name) {
          case 'start': {
            const results = state.results || (state.results = []);
            results.push(response.data);
            break;
          }
        }
    })
});

export const Traceroute: INettoolsPlugin & TReduxPlugin = {
  name: Name,
  use: Nettools,
  redux: { reducer },
  nettools: {
    content: Content,
  },
};
