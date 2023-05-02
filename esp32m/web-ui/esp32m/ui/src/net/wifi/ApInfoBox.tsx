import { CardBox } from '@ts-libs/ui-app';
import { useModuleState } from '../../backend';
import { netmask2cidr } from '../../utils';
import { Name, WifiMode, TWifiState } from './types';
import { NameValueList } from '../../app';

export const ApInfoBox = () => {
  const state = useModuleState<TWifiState>(Name);
  if (!state) return null;
  const { ap, mode } = state;
  if (!ap || ![WifiMode.Ap, WifiMode.ApSta].includes(mode)) return null;
  const list = [];
  const { ip = [] } = ap;
  const [addr, gw, mask] = ip;
  if (ap.mac) list.push(['AP MAC', ap.mac]);
  if (ap.ssid) list.push(['SSID', ap.ssid]);
  if (ap.mac) list.push(['MAC', ap.mac]);
  if (state.ch) list.push(['Channel', state.ch]);
  if (addr)
    list.push(['IP address', addr + (mask ? ` / ${netmask2cidr(mask)}` : '')]);
  if (gw) list.push(['IP gateway', gw]);
  if (ap.cli) list.push(['Clients', ap.cli]);
  return (
    <CardBox title="WiFi Access Point">
      <NameValueList list={list} />
    </CardBox>
  );
};
