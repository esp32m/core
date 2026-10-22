import { Devices } from '../shared';
import { IDevicePlugin } from '../types';
import Content from './Content';

export { default as ThermostatContent } from './Content';
export * from './types';

/** Add only for an explicitly attached firmware thermostat instance. */
export const Thermostat = (name: string, title?: string): IDevicePlugin => ({
  name,
  use: Devices,
  device: { component: Content, props: { name, title } },
});
