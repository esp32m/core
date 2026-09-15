import { Devices } from '../shared';
import { IDevicePlugin } from '../types';
import Content from './Content';

export { default as LfBrosContent } from './Content';
export * from './types';

export const LfBros = (
  name: string,
  title?: string
): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Content, props: { name, title } },
});
