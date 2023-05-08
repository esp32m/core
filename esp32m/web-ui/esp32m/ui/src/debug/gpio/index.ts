import { Debug } from '../shared';
import { TDebugPlugin } from '../types';
import content from './Content';
import { Name } from './types';

export const DebugGpio: TDebugPlugin = {
  name: Name,
  use: Debug,
  debug: { content },
};
