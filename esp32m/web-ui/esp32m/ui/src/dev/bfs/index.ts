import { Devices } from '../shared';
import { IDevicePlugin } from '../types';
import { Content } from './Content';
import { Name, TProps } from './types';

export const Bfs = (props?: TProps): IDevicePlugin => ({
  name: props?.name || Name,
  use: Devices,
  device: { component: Content, props },
});
