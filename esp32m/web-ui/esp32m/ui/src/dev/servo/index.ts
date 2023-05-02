import { Devices } from '../shared';
import Content from './Content';
import { IDevicePlugin } from '../types';

export const Servo = (name: string, title?: string): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Content, props: { name, title } },
});
