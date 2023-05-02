import { Debug } from '../shared';
import { IDebugPlugin } from '../types';
import content from './Content';
import { Name } from './types';

export const DebugTasks: IDebugPlugin = {
  name: Name,
  use: Debug,
  debug: { content },
};
