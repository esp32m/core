import { Devices } from '../shared';
import Content from './Content';
import { IDevicePlugin } from '../types';

export const MicrowaveMotionSensor = (
  name: string,
  title = 'Microwave motion sensor'
): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Content, props: { name, title } },
});
