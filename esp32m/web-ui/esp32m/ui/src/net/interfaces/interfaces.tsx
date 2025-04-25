import { Settings } from '@mui/icons-material';
import { Grid, IconButton, MenuItem } from '@mui/material';
import { useModuleConfig, useModuleState } from '../..';
import { netmask2cidr } from '../../utils';
import {
  TConfig,
  TInterfaceConfig,
  TInterfaceState,
  InterfaceRole,
  Name,
  TState,
} from './types';
import {
  DialogForm,
  FieldSelect,
  FieldText,
  useDialogForm,
} from '@ts-libs/ui-forms';
import { useYup } from '../../validation';
import { CardBox } from '@ts-libs/ui-app';
import { NameValueList } from '../../app';
import { useTranslation } from '@ts-libs/ui-i18n';
import { TJsonValue } from '@ts-libs/tools';

const DhcpStatusNames = ['initial', 'started', 'stopped'];

const InterfaceRoles = [
  [InterfaceRole.Default, 'Default'],
  [InterfaceRole.Static, 'Static IP'],
  [InterfaceRole.DhcpClient, 'DHCP client'],
  [InterfaceRole.DhcpServer, 'DHCP server'],
];

const toMenuItem = (e: Array<any>, i: number) => (
  <MenuItem key={i} value={e[0]}>
    {e[1]}
  </MenuItem>
);

const Ipv4Fields = ({ prefix }: { prefix: string }) => {
  return (
    <Grid container spacing={3}>
      <FieldText name={`${prefix}.0`} label="IP address" grid />
      <FieldText name={`${prefix}.2`} label="Netmask" grid />
      <FieldText name={`${prefix}.1`} label="Gateway" grid />
    </Grid>
  );
};

const DnsFields = ({ prefix }: { prefix: string }) => {
  return (
    <Grid container spacing={3}>
      <FieldText name={`${prefix}.0`} label="Primary DNS" grid />
      <FieldText name={`${prefix}.1`} label="Secondary DNS" grid />
      <FieldText name={`${prefix}.2`} label="Fallback DNS" grid />
    </Grid>
  );
};

const validationSchema = useYup((y) =>
  y.object().shape({
    mac: y.string().macaddr().nullable(),
    ip: y.array(y.string().ipv4().nullable()),
    dns: y.array(y.string().ipv4().nullable()),
  })
);

const Interface = ({
  name,
  state,
  config,
  refreshConfig,
  setConfig,
}: {
  name: string;
  state: TInterfaceState;
  config: TInterfaceConfig;
  refreshConfig: () => void;
  setConfig: (v: TJsonValue) => Promise<void>;
}) => {
  //  const api = useBackendApi();
  const [hook, openSettings] = useDialogForm({
    initialValues: config,
    onSubmit: async (values) => {
      console.log(values);
      await setConfig({ [name]: values });
    },
    validationSchema,
  });
  const { t } = useTranslation();

  if (!state) return null;
  const { up, desc, hostname, mac, ip, ipv6, dns, dhcpc, dhcps, prio } = state;
  const list = [];
  if (hostname) list.push(['Hostname', hostname]);
  if (mac) list.push(['MAC address', mac]);
  if (ip[0] && ip[2])
    list.push(['IP address', `${ip[0]} / ${netmask2cidr(ip[2])}`]);
  if (ip[1]) list.push(['IP gateway', ip[1]]);
  if (ipv6?.length) for (const i in ipv6) list.push(['IPv6 address', ipv6[i]]);
  if (dns?.length)
    list.push(['DNS servers', dns.filter((a) => !!a).join(', ')]);
  if (dhcpc != undefined || dhcps != undefined) {
    let dhcp = '';
    if (dhcpc != undefined) dhcp += `client (${DhcpStatusNames[dhcpc]})`;
    if (dhcps != undefined) {
      if (dhcp) dhcp += ' / ';
      dhcp += `server (${DhcpStatusNames[dhcps]})`;
    }
    list.push(['DHCP', dhcp]);
  }
  if (prio != undefined) list.push(['Priority', prio]);
  let title = name;
  if (desc) title += ` (${desc})`;
  if (up != undefined) title += ` - ${t(up ? 'up' : 'down')}`;
  return (
    <CardBox
      title={title}
      action={
        <IconButton
          onClick={() => {
            refreshConfig();
            openSettings();
          }}
        >
          <Settings />
        </IconButton>
      }
    >
      <NameValueList list={list} />
      <DialogForm title={`${t('Configure interface')} ${name}`} hook={hook}>
        <Grid container spacing={3}>
          <FieldSelect name="role" label="Interface role" fullWidth grid>
            {InterfaceRoles.map(toMenuItem)}
          </FieldSelect>
          <FieldText name="mac" label="MAC address" fullWidth grid />
          <Ipv4Fields prefix="ip" />
          <DnsFields prefix="dns" />
        </Grid>
      </DialogForm>
    </CardBox>
  );
};

export const Interfaces = () => {
  const state = useModuleState<TState>(Name);
  const { config, setConfig, refreshConfig } = useModuleConfig<TConfig>(Name);
  if (!state || !config) return null;
  return (
    <>
      {Object.keys(state).map((key) => (
        <Interface
          name={key}
          state={state[key]}
          config={config[key]}
          refreshConfig={refreshConfig}
          setConfig={setConfig}
          key={key}
        />
      ))}
    </>
  );
};
