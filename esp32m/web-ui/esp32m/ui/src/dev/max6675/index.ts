import { Devices } from '../shared';
import { Name } from './types';
import Content from './Content';
import { IDevicePlugin } from '../types';

export const Max6675 = (
  name: string = Name,
  title?: string
): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Content, props: { name, title } },
});
