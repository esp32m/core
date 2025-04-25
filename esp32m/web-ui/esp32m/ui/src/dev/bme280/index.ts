import { Devices } from '../shared';
import Content from './Content';
import { Name, TProps } from './types';
import { IDevicePlugin } from '../types';

export const Bme280 = (props?: TProps): IDevicePlugin => ({
  name: Name,
  use: Devices,
  device: { component: Content, props },
});
