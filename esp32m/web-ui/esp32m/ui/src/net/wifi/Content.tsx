import { Grid } from '@mui/material';

import { ScannerBox } from './ScannerBox';
import { StaInfoBox } from './StaInfoBox';
import { ApInfoBox } from './ApInfoBox';
import { SettingsBox } from './SettingsBox';

export const Content = () => (
  <Grid container>
    <StaInfoBox />
    <ApInfoBox />
    <SettingsBox />
    <ScannerBox />
  </Grid>
);
