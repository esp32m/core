import { Reducer } from 'redux';
import { Name } from './types';
import { Scanners } from '../shared';
import Scanner from './Scanner';
import { IScannerPlugin } from '../types';
import { TReduxPlugin } from '@ts-libs/redux';
import { TResponse } from '../../backend';

const reducer: Reducer = (state = {}, { type, payload }) => {
  const msg = payload as TResponse;
  if (msg && msg.source == Name && msg.name == 'scan')
    switch (type) {
      case 'backend/response':
        return { ...state, scan: msg.data };
      case 'backend/request':
        return { ...state, scan: {} };
    }
  return state;
};

export const ModbusScanner: IScannerPlugin & TReduxPlugin = {
  name: Name,
  use: Scanners,
  redux: { reducer },
  scanner: { component: Scanner },
};
