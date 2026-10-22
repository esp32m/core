import { Debug } from '../shared';
import { TDebugPlugin } from '../types';
import { content } from './content';
import { Name } from './types';

export const DebugRs485 = (): TDebugPlugin => ({
  name: Name,
  use: Debug,
  debug: { content },
});
