import { FeatureType, TDebugPinFeaturePlugin } from './types';
import { FieldText } from '@ts-libs/ui-forms';
import { Grid } from '@mui/material';
import * as Yup from 'yup';

const validationSchema = Yup.object().shape({
  value: Yup.number().min(0).max(1),
});

const component = () => {
  return (
    <Grid container spacing={2}>
      <Grid size={{ xs: 6 }}>
        <FieldText name="value" label="Value" fullWidth type="number" />
      </Grid>
    </Grid>
  );
};

export const DacPlugin: TDebugPinFeaturePlugin = {
  name: 'debug-pin-dac',
  debug: {
    pin: {
      features: [
        { name: 'DAC', feature: FeatureType.DAC, component, validationSchema },
      ],
    },
  },
};
