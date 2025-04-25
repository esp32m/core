import { Content } from './Content';
import { INettoolsPlugin, Nettools } from '..';
import { ILocalState, StartAction, Name } from './types';
import { TReduxPlugin } from '@ts-libs/redux';
import { createReducer } from '@reduxjs/toolkit';
import { actions } from '../../backend';


const reducer = createReducer({} as ILocalState, (builder) => {
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
})


export const Ping: INettoolsPlugin & TReduxPlugin = {
  name: 'ping',
  use: Nettools,
  redux: { reducer },
  nettools: {
    content: Content,
  },
};
