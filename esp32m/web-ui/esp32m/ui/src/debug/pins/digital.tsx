import { FieldSwitch } from '@ts-libs/ui-forms';
import { Grid } from '@mui/material';

export const Digital = () => {
  return (
    <Grid item container spacing={2}>
      <Grid item xs={12}>
        <FieldSwitch name="high" label="LOW / HIGH" />
      </Grid>
    </Grid>
  );
};
