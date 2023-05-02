import { Devices } from '../shared';
import Content from './Content';
import { IDevicePlugin } from '../types';

export const Hbridge = (
  nameOrList: string | Array<[string, string]>,
  title?: string
): IDevicePlugin => ({
  name: 'hbridge-' + nameOrList,
  use: Devices,
  device: { component: Content, props: { nameOrList, title } },
});
