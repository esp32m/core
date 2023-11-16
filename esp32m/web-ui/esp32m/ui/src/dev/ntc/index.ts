import { Devices } from '../shared';
import Content from './Content';
import { IDevicePlugin } from '../types';

export const NtcSensor = (
  name: string,
  title = 'NTC sensor'
): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Content, props: { name, title } },
});
