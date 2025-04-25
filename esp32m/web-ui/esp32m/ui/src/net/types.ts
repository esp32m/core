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
export type TNetInterface = [string, string, number, string];
export type TNetInterfaces = Array<TNetInterface>;

export type TIpv4Info = [ip: string, gw: string, netmask: string];
