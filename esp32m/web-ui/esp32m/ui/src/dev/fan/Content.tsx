import React from 'react';
import { TState, TProps } from './types';
import * as Backend from '../../backend';
import { NameValueList, useModuleState } from '../..';
import {
  Slider,
  Grid,
  Typography,
  FormControlLabel,
  Switch,
} from '@mui/material';
import { CardBox } from '@ts-libs/ui-app';
import { isNumber } from '@ts-libs/tools';

export default ({ name, title }: TProps) => {
  const state = useModuleState<TState>(name);
  const [disabled, setDisabled] = React.useState(false);
  const [localSpeed, setLocalSpeed] = React.useState(state?.speed || -1);
  const api = Backend.useBackendApi();
  if (!state) return null;
  const { rpm, on } = state;
  if (localSpeed < 0) setLocalSpeed(state.speed || 0);
  return (
    <CardBox title={title || name}>
      <Grid container>
        <FormControlLabel
          control={
            <Switch
              disabled={disabled}
              checked={on}
              onChange={(e) => {
                setDisabled(true);
                api
                  .setState(name, { on: e.target.checked })
                  .finally(() => setDisabled(false));
              }}
            />
          }
          label="Enabled"
        />
        {isNumber(rpm) && (
          <Grid size={{ xs: 12 }}>
            <NameValueList
              list={[['Actual speed', `${(rpm || 0).toFixed(1)} RPM`]]}
            />
          </Grid>
        )}
        <Grid size={{ xs: 12 }} container style={{ marginTop: 15 }}>
          <Grid size={{ xs: 3 }}>
            <Typography>Set fan speed</Typography>
          </Grid>
          <Grid size={{ xs: 9 }}>
            <Slider
              min={0}
              max={100}
              valueLabelDisplay="auto"
              value={localSpeed * 100}
              onChange={(_, value) => {
                setLocalSpeed((value as number) / 100);
              }}
              valueLabelFormat={(value) => `${value} %`}
              onChangeCommitted={(_, value) => {
                setDisabled(true);
                const speed = (value as number) / 100;
                api.setState(name, { speed }).finally(() => {
                  setDisabled(false);
                });
              }}
              disabled={disabled}
            />
          </Grid>
        </Grid>
      </Grid>
    </CardBox>
  );
};
