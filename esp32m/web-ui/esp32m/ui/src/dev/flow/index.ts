import { Devices } from '../shared';
import { IDevicePlugin } from '../types';
import Content from './Content';

export const FlowSensor = (
  name: string,
  title = 'Flow sensor'
): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Content, props: { name, title } },
});
