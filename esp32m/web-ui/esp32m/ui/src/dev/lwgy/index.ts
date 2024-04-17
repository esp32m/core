import { Devices } from '../shared';
import { IDevicePlugin } from '../types';
import { Content } from './Content';
import { Name, TProps } from './types';

export const Lwgy = (props?: TProps): IDevicePlugin => ({
  name: Name,
  use: Devices,
  device: { component: Content, props },
});
