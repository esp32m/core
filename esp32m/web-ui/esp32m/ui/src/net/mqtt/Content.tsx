import { Grid } from '@mui/material';

import { Config } from './Config';
import { MqttStateBox } from './State';

export const Content = () => (
  <Grid container>
    <MqttStateBox />
    <Config />
  </Grid>
);
