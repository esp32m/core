import { CardBox } from '@ts-libs/ui-app';
import { useModuleState } from '../../backend';
import { netmask2cidr, rssiToStr } from '../../utils';
import { Name, WifiMode, TWifiState } from './types';
import { NameValueList } from '../../app';

export const StaInfoBox = () => {
  const state = useModuleState<TWifiState>(Name);
  const { sta, mode = 0, ch } = state || {};
  if (!sta || ![WifiMode.Sta, WifiMode.ApSta].includes(mode)) return null;
  const list = [];
  const { ssid, ip = [] } = sta;
  const { mac, bssid, rssi } = sta;
  const [addr, gw, mask] = ip;
  if (mac) list.push(['STA MAC', mac]);
  if (ssid) list.push(['SSID', ssid + (bssid ? ` (${bssid})` : '')]);
  if (ch) list.push(['Channel', ch]);
  if (addr)
    list.push(['IP address', addr + (mask ? ` / ${netmask2cidr(mask)}` : '')]);
  if (gw) list.push(['IP gateway', gw]);
  if (rssi) list.push(['RSSI', rssiToStr(rssi)]);
  return (
    <CardBox title="WiFi station">
      <NameValueList list={list} />
    </CardBox>
  );
};
