import {
  FeatureType,
  PcntEdgeAction,
  PcntLevelAction,
  TDebugPinFeaturePlugin,
  TPcntState,
} from './types';
import { FieldSelect, FieldSwitch, FieldText } from '@ts-libs/ui-forms';
import { Grid, MenuItem } from '@mui/material';
import * as Yup from 'yup';
import { useMemo } from 'react';
import { useFeatureState } from './hooks';

const validationSchema = Yup.object().shape({
  gns: Yup.number().integer().min(0),
  hla: Yup.number().integer().min(0),
  lla: Yup.number().integer().min(0),
  pea: Yup.number().integer().min(0),
  nea: Yup.number().integer().min(0),
});

const beforeSubmit = (values: TPcntState) => {
  if (values) {
    const v = { ...values };
    if (typeof v.hla === 'string') v.hla = parseInt(v.hla);
    if (typeof v.lla === 'string') v.lla = parseInt(v.lla);
    if (typeof v.pea === 'string') v.pea = parseInt(v.pea);
    if (typeof v.nea === 'string') v.nea = parseInt(v.nea);
    return v;
  }
  return values;
};

const LevelActionNames = {
  [PcntLevelAction.Keep]: 'Keep',
  [PcntLevelAction.Inverse]: 'Invert',
  [PcntLevelAction.Hold]: 'Hold',
};
const EdgeActionNames = {
  [PcntEdgeAction.Hold]: 'Hold',
  [PcntEdgeAction.Increase]: 'Increase',
  [PcntEdgeAction.Decrease]: 'Decrease',
};

const component = () => {
  const levelActions = useMemo(
    () =>
      Object.entries(LevelActionNames).map(([value, name], i) => (
        <MenuItem value={value} key={i}>
          {name}
        </MenuItem>
      )),
    []
  );
  const edgeActions = useMemo(
    () =>
      Object.entries(EdgeActionNames).map(([value, name], i) => (
        <MenuItem value={value} key={i}>
          {name}
        </MenuItem>
      )),
    []
  );
  const { state } = useFeatureState<TPcntState>() || {};

  return (
    <Grid item container spacing={2}>
      <Grid item xs={12}>
        <FieldSwitch name="enabled" label="Pulse counter enabled" />
      </Grid>
      <Grid item xs={6}>
        <FieldSelect name="hla" label="High level action" fullWidth>
          {levelActions}
        </FieldSelect>
      </Grid>
      <Grid item xs={6}>
        <FieldSelect name="lla" label="Low level action" fullWidth>
          {levelActions}
        </FieldSelect>
      </Grid>
      <Grid item xs={6}>
        <FieldSelect name="pea" label="Positive edge action" fullWidth>
          {edgeActions}
        </FieldSelect>
        D
      </Grid>
      <Grid item xs={6}>
        <FieldSelect name="nea" label="Neagtive edge action" fullWidth>
          {edgeActions}
        </FieldSelect>
      </Grid>
      <Grid item xs={6}>
        <FieldText name="gns" label="Filter, ns" fullWidth type="number" />
      </Grid>
      {state && (
        <Grid item xs={12}>
          {`Counter value: ${state.value}, frequency: ${state.freq?.toFixed(3) || 0} Hz`}
        </Grid>
      )}
    </Grid>
  );
};

export const PcntPlugin: TDebugPinFeaturePlugin = {
  name: 'debug-pin-pcnt',
  debug: {
    pin: {
      features: [
        {
          name: 'PulseCounter',
          feature: FeatureType.Pcnt,
          component,
          validationSchema,
          beforeSubmit,
        },
      ],
    },
  },
};
