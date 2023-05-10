import {
  AdcAtten,
  FeatureType,
  TADCState,
  TDebugPinFeaturePlugin,
} from './types';
import { FieldSelect, FieldText } from '@ts-libs/ui-forms';
import { Grid, MenuItem } from '@mui/material';
import { useMemo } from 'react';
import * as Yup from 'yup';
import { useFeatureState } from './hooks';

const validationSchema = Yup.object().shape({
  width: Yup.number().integer().min(9).max(13),
});

const AttenNames = {
  [AdcAtten.Db0]: 'No attenuation',
  [AdcAtten.Db2_5]: '2.5 dB (1.33x)',
  [AdcAtten.Db6]: '6 dB (2x)',
  [AdcAtten.Db11]: '11 dB (3.55x)',
};

const component = () => {
  const attenOptions = useMemo(
    () =>
      Object.entries(AttenNames).map(([value, name], i) => (
        <MenuItem value={value} key={i}>
          {name}
        </MenuItem>
      )),
    []
  );
  const { state } = useFeatureState<TADCState>() || {};
  return (
    <Grid item container spacing={2}>
      <Grid item xs={6}>
        <FieldSelect name="atten" label="Attenuation" fullWidth type="number">
          {attenOptions}
        </FieldSelect>
      </Grid>
      <Grid item xs={6}>
        <FieldText name="width" type="number" label="Bit width" fullWidth />
      </Grid>
      {state && (
        <Grid item xs={12}>
          {`Signal level: ${(state.value * 100).toFixed(2)} %, ${state.mv} mv`}
        </Grid>
      )}
    </Grid>
  );
};

export const AdcPlugin: TDebugPinFeaturePlugin = {
  name: 'debug-pin-adc',
  debug: {
    pin: {
      features: [
        { name: 'ADC', feature: FeatureType.ADC, component, validationSchema },
      ],
    },
  },
};
