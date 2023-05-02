import { NetInterfaces } from "../types";

export const Name = 'traceroute';
export const StartAction = 'traceroute/start';

export interface IConfig {
  target: string;
  timeout: number;
  proto: number;
  iface: number;
  interfaces: NetInterfaces;
}

export interface IResult {
  ip: string;
  seq: number;
  row?: number;
  ttls: number;
  ttlr?: number;
  tus?: number;
  icmpc?: number;
}

export interface ILocalState {
  results?: Array<IResult>;
}
