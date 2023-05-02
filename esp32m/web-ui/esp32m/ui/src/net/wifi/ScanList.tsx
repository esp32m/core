import { useState } from 'react';

import { List, ListItem, ListItemText, ListItemIcon } from '@mui/material';

import { Name, TWifiState, WifiAuth, ScanEntries } from './types';
import { SignalIcon } from './SignalIcon';
import ConnectDialog, { IProps as IConnectDialogProps } from './ConnectDialog';
import { styled } from '@mui/material/styles';
import { useModuleState } from '../../backend';
import { useConnector } from './utils';

const StyledListItem = styled(ListItem)(({ theme }) => ({
  cursor: 'pointer',
  '&:hover': {
    backgroundColor:
      theme.palette.mode === 'light'
        ? 'rgba(0, 0, 0, 0.07)' // grey[200]
        : 'rgba(255, 255, 255, 0.14)',
  },
}));

export const ScanList = ({ scan }: { scan: ScanEntries }) => {
  const state = useModuleState<TWifiState>(Name);
  const connector = useConnector();
  const [dialogProps, setDialogProps] = useState<IConnectDialogProps>({
    open: false,
    ssid: '',
    bssid: '',
    onClose: handleConnect,
  });

  function handleConnect(ssid?: string, bssid?: string, password?: string) {
    if (ssid) connector(ssid, bssid, password);
    setDialogProps({ ...dialogProps, open: false });
  }

  function handleClick(ssid: string, bssid: string, auth: WifiAuth) {
    const open = !!auth;
    if (open) setDialogProps({ ...dialogProps, open: true, ssid, bssid });
    else connector(ssid, bssid);
  }

  const { sta } = state || {};
  let scanTable;
  if (scan)
    scanTable = (
      <List>
        {scan.map((r, i) => {
          const [ssid, auth, rssi, ch, bssid] = r;
          const c =
            sta && ssid && bssid == sta.bssid ? { fontWeight: 600 } : {};
          return (
            <StyledListItem
              key={'row' + i}
              onClick={() => handleClick(ssid, bssid, auth)}
            >
              <ListItemText
                style={c}
                primary={ssid || '[hidden]'}
                secondary={bssid}
              />
              <ListItemIcon>
                <SignalIcon {...{ rssi, auth, ch, bssid }} />
              </ListItemIcon>
            </StyledListItem>
          );
        })}
      </List>
    );
  return (
    <>
      {scanTable}
      <ConnectDialog {...dialogProps} />
    </>
  );
};
