import { FieldSwitch } from '@ts-libs/ui-forms';
import { FormControlLabel, Grid, Switch } from '@mui/material';
import { useField, useFormikContext } from 'formik';
import { useSelector } from 'react-redux';
import { selectors } from './state';
import {
  FeatureType,
  PinFlags,
  PinMode,
  TDebugPinFeaturePlugin,
} from './types';

const Mode = () => {
  const pinState = useSelector(selectors.pinState);
  const [field] = useField<PinMode>('mode');
  const { setFieldValue } = useFormikContext();
  if (!pinState || !field) return null;
  const { flags } = pinState;
  const mode = field.value;
  const enable = (m: PinMode, on: boolean) =>
    setFieldValue('mode', on ? mode | m : mode & ~m);
  return (
    <Grid item xs={12}>
      {(flags & PinFlags.Input) != 0 && (
        <FormControlLabel
          control={
            <Switch
              checked={(mode & PinMode.Input) != 0}
              onChange={(e, checked) => enable(PinMode.Input, checked)}
            />
          }
          label="Enable input"
        />
      )}
      {(flags & PinFlags.Output) != 0 && (
        <FormControlLabel
          control={
            <Switch
              checked={(mode & PinMode.Output) != 0}
              onChange={(e, checked) => enable(PinMode.Output, checked)}
            />
          }
          label="Enable output"
        />
      )}
      {(flags & PinFlags.PullUp) != 0 && (
        <FormControlLabel
          control={
            <Switch
              checked={(mode & PinMode.PullUp) != 0}
              onChange={(e, checked) => enable(PinMode.PullUp, checked)}
            />
          }
          label="Enable pull up"
        />
      )}
      {(flags & PinFlags.PullDown) != 0 && (
        <FormControlLabel
          control={
            <Switch
              checked={(mode & PinMode.PullDown) != 0}
              onChange={(e, checked) => enable(PinMode.PullDown, checked)}
            />
          }
          label="Enable pull down"
        />
      )}
      {(flags & PinFlags.OpenDrain) != 0 && (
        <FormControlLabel
          control={
            <Switch
              checked={(mode & PinMode.OpenDrain) != 0}
              onChange={(e, checked) => enable(PinMode.OpenDrain, checked)}
            />
          }
          label="Enable open-drain output"
        />
      )}
    </Grid>
  );
};

const component = () => {
  return (
    <Grid item container spacing={2}>
      <Mode />
      <Grid item xs={12}>
        <FieldSwitch name="high" label="Digital state (LOW / HIGH)" />
      </Grid>
    </Grid>
  );
};

export const DigitalPlugin: TDebugPinFeaturePlugin = {
  name: 'debug-pin-digital',
  debug: {
    pin: {
      features: [{ name: 'Digital', feature: FeatureType.Digital, component }],
    },
  },
};
