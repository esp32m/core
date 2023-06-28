import { Navigate } from 'react-router-dom';
import { Grid, Typography } from '@mui/material';

import { ScannerBox } from './ScannerBox';
import { StaInfoBox } from './StaInfoBox';
import { ApInfoBox } from './ApInfoBox';
import { TContentPlugin, TRoutesPlugin } from '@ts-libs/ui-base';

import { CardBox } from '@ts-libs/ui-app';
import { useModuleState } from '../../backend';
import { Name, WifiMode, TWifiState } from './types';

const SuccessBox = () => {
  const state = useModuleState<TWifiState>(Name);
  const { sta, mode = 0 } = state || {};
  if (!sta || ![WifiMode.Sta, WifiMode.ApSta].includes(mode)) return null;
  const { ssid, ip = [], bssid } = sta;
  const [addr] = ip;
  if (!ssid || !addr) return null;
  return (
    <CardBox>
      <Typography align="center" variant="h6">
        Connection succeeded!
      </Typography>
      <Typography align="center" variant="subtitle1">
        {`The device is connected to the WiFi access point ${
          ssid + (bssid ? ` (${bssid})` : '')
        }`}
      </Typography>
      <Typography align="center" variant="subtitle2">
        {`You can now switch to your local WiFi and access this device via http://${addr}/`}
      </Typography>
    </CardBox>
  );
};

const component = () => (
  <Grid container>
    <SuccessBox />
    <ScannerBox />
    <StaInfoBox />
    <ApInfoBox />
  </Grid>
);

export const CaptivePortal: TContentPlugin & TRoutesPlugin = {
  name: 'cp',
  content: { component },
  routes: [
    {
      path: '/generate_204',
      element: <Navigate to="/cp" />,
    },
  ],
};
