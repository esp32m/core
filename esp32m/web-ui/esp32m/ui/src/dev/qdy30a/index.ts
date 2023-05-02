import { Devices } from '../shared';
import { IDevicePlugin } from '../types';
import Content from './Content';
import { Name, IProps } from './types';

export const Qdy30a = (props?: IProps): IDevicePlugin => ({
  name: Name,
  use: Devices,
  device: { component: Content, props },
});
