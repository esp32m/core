import { Devices } from '../shared';
import Content from './Content';
import { IDevicePlugin } from '../types';

export const MoistureSensor = (
  name: string,
  title = 'Moisture sensor'
): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Content, props: { name, title } },
});
