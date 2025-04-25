import { useFormikContext } from 'formik';
import { FeatureType, TDebugPinFeaturePlugin, TPWMState } from './types';
import { FieldSwitch, FieldText } from '@ts-libs/ui-forms';
import { Grid } from '@mui/material';
import * as Yup from 'yup';

const validationSchema = Yup.object().shape({
  freq: Yup.number().integer().min(0).max(1000000),
});

const component = () => {
  const {
    values: { enabled },
  } = useFormikContext<TPWMState>();
  return (
    <Grid  container spacing={2}>
      <Grid size={{ xs: 12 }}>
        <FieldSwitch name="enabled" label="PWM generator enabled" />
      </Grid>

      {enabled && (
        <>
          <Grid size={{ xs: 6 }}>
            <FieldText
              name="freq"
              type="number"
              label="Frequency, Hz"
              fullWidth
            />
          </Grid>
          <Grid size={{ xs: 6 }}>
            <FieldText
              name="duty"
              type="number"
              label="Duty cycle, %"
              fullWidth
            />
          </Grid>
        </>
      )}
    </Grid>
  );
};

export const PwmPlugin: TDebugPinFeaturePlugin = {
  name: 'debug-pin-pwm',
  debug: {
    pin: {
      features: [
        { name: 'PWM', feature: FeatureType.PWM, component, validationSchema },
      ],
    },
  },
};
