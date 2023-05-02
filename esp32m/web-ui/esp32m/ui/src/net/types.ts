import { TPlugin } from "@ts-libs/plugins";
import React from "react";

export type INetworkPlugin = TPlugin & {
  network: {
    content: React.ComponentType;
    props?: React.Attributes;
  };
};

export type INettoolsPlugin = TPlugin & {
  nettools: {
    content: React.ComponentType;
    props?: React.Attributes;
  };
};
export type NetInterface = [string, string, number, string];
export type NetInterfaces = Array<NetInterface>;

export type TIpv4Info = [ip: string, gw: string, netmask: string];
