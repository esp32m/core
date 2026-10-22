import { Devices } from '../shared';
import type { IDevicePlugin } from '../types';
import { Content } from './Content';
import { Name } from './types';
import type { TProps } from './types';

export const HsmWt10 = (props?: TProps): IDevicePlugin => ({
  name: props?.name || Name,
  use: Devices,
  device: { component: Content, props },
});
