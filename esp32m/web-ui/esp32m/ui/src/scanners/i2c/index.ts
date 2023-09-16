import { Name } from './types';
import { Scanners } from '../shared';
import { Scanner } from './Scanner';
import { IScannerPlugin } from '../types';

export const I2CScanner: IScannerPlugin = {
  name: Name,
  use: Scanners,
  scanner: { component: Scanner },
};
