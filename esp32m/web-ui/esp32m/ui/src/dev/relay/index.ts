import { Devices } from '../shared';
import { IDevicePlugin } from '../types';
import Content from './Content';
import { NameOrList } from './types';

export const Relay = (
  nameOrList: NameOrList,
  title?: string
): IDevicePlugin => ({
  name: 'relay',
  use: Devices,
  device: { component: Content, props: { nameOrList, title } },
});
