import { Devices } from '../shared';
import Content from './Content';
import { Name, IProps } from './types';
import { IDevicePlugin } from '../types';

export const Dds238 = (props?: IProps): IDevicePlugin => ({
  name: Name,
  use: Devices,
  device: { component: Content, props },
});
