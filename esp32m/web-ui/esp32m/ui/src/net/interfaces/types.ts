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

export type TInterfaceState = {
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

export type TState = Record<string, TInterfaceState>;

export type TInterfaceConfig = {
  role: InterfaceRole;
  mac: string;
  ip: [string, string, string];
  ipv6: Array<string>;
  dns: Array<string>;
}

export type TConfig = Record<string, TInterfaceConfig>;
