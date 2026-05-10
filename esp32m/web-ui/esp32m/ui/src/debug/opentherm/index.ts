import { Debug } from '../shared';
import { TDebugPlugin } from '../types';
import { content } from './content';
import { Name } from './types';

export * from './types';

export const DebugOpenthermMaster: TDebugPlugin = {
  name: Name,
  use: Debug,
  debug: { content },
};
