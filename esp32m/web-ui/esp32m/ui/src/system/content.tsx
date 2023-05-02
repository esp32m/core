import { Grid } from '@mui/material';

import { HardwareSummary } from './hardware-summary';
import { AppSummary } from './app-summary';
import { Admin } from './Admin';
import { Settings } from './settings';
import { AppSettings } from './app-settings';

export const Content = () => (
  <Grid container>
    <AppSummary />
    <HardwareSummary />
    <AppSettings />
    <Admin />
    <Settings />
  </Grid>
);
