import { Devices } from '../shared';
import Master from './Master';
import Slave from './Slave';
import { IDevicePlugin } from '../types';

export const OpenthermMaster = (
  name: string,
  title?: string
): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Master, props: { name, title } },
});

export const OpenthermSlave = (
  name: string,
  title?: string
): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Slave, props: { name, title } },
});
