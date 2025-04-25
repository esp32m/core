import { TNetInterfaces } from "../types";
import { createAction } from '@reduxjs/toolkit';

export const Name = 'traceroute';

export const StartAction = createAction('traceroute/start');

export type TConfig = {
  target: string;
  timeout: number;
  proto: number;
  iface: number;
  interfaces: TNetInterfaces;
}

export type TResult = {
  ip: string;
  seq: number;
  row?: number;
  ttls: number;
  ttlr?: number;
  tus?: number;
  icmpc?: number;
}

export type TLocalState = {
  results?: Array<TResult>;
}
