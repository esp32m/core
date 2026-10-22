import { Devices } from '../shared';
import { IDevicePlugin } from '../types';
import Content from './Content';

export { default as CdhxContent } from './Content';
export * from './types';

export const Cdhx = (name = 'cdhx', title?: string): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Content, props: { name, title } },
});
