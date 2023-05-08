import { TReduxPlugin } from '@ts-libs/redux';
import { Debug } from '../shared';
import { TDebugPlugin } from '../types';
import { content } from './content';
import { Name, reducer } from './state';

export const DebugPins = (): TDebugPlugin & TReduxPlugin => ({
  name: Name,
  use: Debug,
  redux: { reducer },
  debug: { content },
});
