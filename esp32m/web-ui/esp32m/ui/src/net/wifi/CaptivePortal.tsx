import { Navigate } from 'react-router-dom';
import { Grid } from '@mui/material';

import { ScannerBox } from './ScannerBox';
import { StaInfoBox } from './StaInfoBox';
import { ApInfoBox } from './ApInfoBox';
import { TContentPlugin, TRoutesPlugin } from '@ts-libs/ui-base';

const component = () => (
  <Grid container>
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
