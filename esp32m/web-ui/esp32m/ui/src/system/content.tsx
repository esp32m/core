import { Grid } from '@mui/material';

import { HardwareSummary } from './hardware-summary';
import { AppSummary } from './app-summary';
import { Admin } from './Admin';
import { Settings } from './settings';
import { AppSettings } from './app-settings';
import { Logs } from './logs';

export const Content = () => (
  <Grid
    container
    sx={{
      '@media (max-width: 599.95px)': {
        '& > form': { width: '100%', minWidth: 0 },
        '& > .MuiGrid-root, & > form > .MuiGrid-root': {
          minWidth: 0,
          width: 'calc(100% - 32px)',
          maxWidth: 'calc(100% - 32px)',
          margin: 2,
        },
        '& .MuiTableCell-root': { overflowWrap: 'anywhere' },
      },
    }}
  >
    <AppSummary />
    <HardwareSummary />
    <AppSettings />
    <Logs />
    <Admin />
    <Settings />
  </Grid>
);
