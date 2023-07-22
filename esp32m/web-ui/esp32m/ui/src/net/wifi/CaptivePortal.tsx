import { Navigate } from 'react-router-dom';
import { Grid, Typography } from '@mui/material';

import { ScannerBox } from './ScannerBox';
import { StaInfoBox } from './StaInfoBox';
import { ApInfoBox } from './ApInfoBox';
import { TContentPlugin, TRoutesPlugin } from '@ts-libs/ui-base';

import { CardBox } from '@ts-libs/ui-app';
import { useModuleState } from '../../backend';
import { Name, WifiMode, TWifiState } from './types';
import { useTranslation } from '@ts-libs/ui-i18n';

const SuccessBox = () => {
  const { t } = useTranslation();
  const state = useModuleState<TWifiState>(Name);
  const { sta, mode = 0 } = state || {};
  if (!sta || ![WifiMode.Sta, WifiMode.ApSta].includes(mode)) return null;
  const { ssid, ip = [], bssid } = sta;
  const [addr] = ip;
  if (!ssid || !addr) return null;
  const ssidText = `${ssid + (bssid ? ` (${bssid})` : '')}`;
  return (
    <CardBox>
      <Typography align="center" variant="h6">
        {t('Connection succeeded!')}
      </Typography>
      <Typography align="center" variant="subtitle1">
        {t('CaptivePortalConnected', { ssidText })}
      </Typography>
      <Typography align="center" variant="subtitle2">
        {t('CaptivePortalDeviceURL', { addr })}
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
