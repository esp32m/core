import React from 'react';
import { WifiAuth } from './types';
import {
  SignalWifi0Bar,
  SignalWifi1Bar,
  SignalWifi1BarLock,
  SignalWifi2Bar,
  SignalWifi2BarLock,
  SignalWifi3Bar,
  SignalWifi3BarLock,
  SignalWifi4Bar,
  SignalWifi4BarLock,
} from '@mui/icons-material';
import { Tooltip } from '@mui/material';
import { rssiToLevel, rssiToStr } from '../../utils';

const iconsN = [
  SignalWifi0Bar,
  SignalWifi1Bar,
  SignalWifi2Bar,
  SignalWifi3Bar,
  SignalWifi4Bar,
];
const iconsL = [
  SignalWifi1BarLock,
  SignalWifi1BarLock,
  SignalWifi2BarLock,
  SignalWifi3BarLock,
  SignalWifi4BarLock,
];

interface IProps {
  rssi: number;
  auth: WifiAuth;
  ch: number;
  bssid: string;
}

const AuthNames = [
  null,
  'WEP',
  'WPA-PSK',
  'WPA2-PSK',
  'WPA-WPA2-PSK',
  'WPA2-ENTERPRISE',
];

export function SignalIcon({ rssi, auth, ch }: IProps) {
  const I: React.ComponentType = (auth ? iconsL : iconsN)[rssiToLevel(rssi, 5)];
  const tt = (
    <>
      <div>{'signal: ' + rssiToStr(rssi)}</div>
      {!!auth && <div> {'auth: ' + AuthNames[auth]}</div>}
      {ch && <div>{'channel: ' + ch}</div>}
    </>
  );
  return (
    <Tooltip title={tt}>
      <I />
    </Tooltip>
  );
}
