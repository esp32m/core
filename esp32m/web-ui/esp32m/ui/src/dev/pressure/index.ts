import { Devices } from '../shared';
import { IDevicePlugin } from '../types';
import Content from './Content';

export const PressureSensor = (
  name: string,
  title = 'Pressure sensor'
): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Content, props: { name, title } },
});
