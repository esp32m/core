export const Name = 'netifs';

export const enum DhcpStatus {
  Initial,
  Started,
  Stopped,
}

export const enum InterfaceRole {
  Default,
  Static,
  DhcpClient,
  DhcpServer,
}

export interface IInterfaceState {
  up: boolean;
  desc: string;
  hostname: string;
  mac: string;
  ip: [string, string, string];
  ipv6: Array<string>;
  dns: Array<string>;
  dhcpc?: DhcpStatus;
  dhcps?: DhcpStatus;
  prio: number;
}

export type State = Record<string, IInterfaceState>;

export interface IInterfaceConfig {
  role: InterfaceRole;
  mac: string;
  ip: [string, string, string];
  ipv6: Array<string>;
  dns: Array<string>;
}

export type Config = Record<string, IInterfaceConfig>;
