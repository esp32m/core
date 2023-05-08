import { useFormikContext } from 'formik';
import { TPWMState } from './types';
import { FieldSwitch, FieldText } from '@ts-libs/ui-forms';
import { Grid } from '@mui/material';

export const PWM = () => {
  const {
    values: { enabled },
  } = useFormikContext<TPWMState>();
  return (
    <Grid item container spacing={2}>
      <Grid item xs={12}>
        <FieldSwitch name="enabled" label="PWM generator enabled" />
      </Grid>

      {enabled && (
        <>
          <Grid item xs={6}>
            <FieldText
              name="freq"
              type="number"
              label="Frequency, Hz"
              fullWidth
            />
          </Grid>
          <Grid item xs={6}>
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
