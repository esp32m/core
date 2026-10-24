import {
  DeviceDisabledAlert,
  DeviceDisableSetting,
  useDeviceControl,
} from '../DeviceControl';
import { useEffect, useState } from 'react';
import {
  Alert,
  Button,
  Grid,
  IconButton,
  MenuItem,
  Slider,
  Typography,
} from '@mui/material';
import { Settings } from '@mui/icons-material';
import { CardBox } from '@ts-libs/ui-app';
import {
  DialogForm,
  FieldSelect,
  FieldText,
  useDialogForm,
} from '@ts-libs/ui-forms';
import { useSnackApi } from '@ts-libs/ui-snack';
import * as Yup from 'yup';
import { NameValueList } from '../../app';
import {
  useBackendApi,
  useModuleConfig,
  useModuleState,
  useSampleAge,
} from '../..';
import { Name, TConfig, TProps, TState } from './types';

const BaudRates = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200];

const validationSchema = Yup.object().shape({
  address: Yup.number().integer().min(1).max(254).required(),
  baud: Yup.number().integer().oneOf(BaudRates).required(),
});

export const Content = ({ title = 'Flomarvel valve', addr }: TProps) => {
  const state = useModuleState<TState>(Name);
  const actualAge = useSampleAge(state?.actualSampledAtMs, Name);
  const api = useBackendApi();
  const deviceControl = useDeviceControl(Name);
  const { config, setConfig } = useModuleConfig<TConfig>(Name);
  const snackApi = useSnackApi();
  const [disabled, setDisabled] = useState(false);
  const [angle, setAngle] = useState(0);
  const [configHook, openSettings] = useDialogForm({
    initialValues: config || {},
    onSubmit: async (values) => {
      if (deviceControl.disabled) return;
      return setConfig({ address: values.address, baud: values.baud });
    },
    validationSchema,
  });

  useEffect(() => {
    if (state?.targetValid) setAngle(state.target);
  }, [state?.target, state?.targetValid]);

  if (!state) return null;
  if (addr !== undefined && addr !== state.address) return null;

  const move = async (nextAngle: number) => {
    try {
      setDisabled(true);
      await api.setState(Name, { angle: nextAngle });
    } catch (e) {
      snackApi.error(e);
    } finally {
      setDisabled(false);
    }
  };
  const command = async (state: 'open' | 'close') => {
    try {
      setDisabled(true);
      await api.setState(Name, { state });
    } catch (e) {
      snackApi.error(e);
    } finally {
      setDisabled(false);
    }
  };
  const stale = !state.actualValid || actualAge < 0 || actualAge > 10_000;
  const actual = stale ? 'Unknown' : `${state.actual.toFixed(2)}°`;
  const commanded = state.targetValid
    ? `${state.target.toFixed(2)}°`
    : 'Unknown';
  const list: Array<[string, string | number]> = [
    ['Actual / commanded position', `${actual} / ${commanded}`],
    ['Valve state', state.state],
    ['Valve position', state.position >= 0 ? `${state.position}%` : 'Unknown'],
  ];

  return (
    <CardBox
      title={title}
      action={
        <IconButton onClick={() => openSettings()}>
          <Settings />
        </IconButton>
      }
    >
      <DeviceDisabledAlert control={deviceControl} />
      {stale && !deviceControl.disabled && (
        <Alert severity="warning">
          No recent valve response. Actual position is unavailable.
        </Alert>
      )}
      <NameValueList list={list} />
      <Grid container spacing={2} sx={{ marginTop: 1 }}>
        <Grid size={{ xs: 12 }}>
          <Button
            variant={
              state.state === 'closed' || state.state === 'closing'
                ? 'contained'
                : 'outlined'
            }
            disabled={disabled || deviceControl.disabled || deviceControl.busy}
            onClick={() => command('close')}
            sx={{ marginRight: 1 }}
          >
            Close
          </Button>
          <Button
            variant={
              state.state === 'open' || state.state === 'opening'
                ? 'contained'
                : 'outlined'
            }
            disabled={disabled || deviceControl.disabled || deviceControl.busy}
            onClick={() => command('open')}
          >
            Open
          </Button>
        </Grid>
        <Grid size={{ xs: 12 }}>
          <Typography>Set position</Typography>
          <Slider
            min={0}
            max={90}
            step={0.01}
            marks={[
              { value: 0, label: '0°' },
              { value: 45, label: '45°' },
              { value: 90, label: '90°' },
            ]}
            value={angle}
            valueLabelDisplay="auto"
            valueLabelFormat={(value) => `${value}°`}
            disabled={disabled || deviceControl.disabled || deviceControl.busy}
            onChange={(_, value) => setAngle(value as number)}
            onChangeCommitted={(_, value) => move(value as number)}
          />
        </Grid>
      </Grid>
      <DialogForm title="Configure Modbus" hook={configHook}>
        <DeviceDisableSetting control={deviceControl} busy={disabled} />
        <Grid container spacing={3}>
          <FieldText
            disabled={deviceControl.disabled || deviceControl.busy}
            name="address"
            label="Slave address"
            type="number"
            grid
          />
          <FieldSelect
            disabled={deviceControl.disabled || deviceControl.busy}
            name="baud"
            label="Baud rate"
            type="number"
            grid
          >
            {BaudRates.map((baud) => (
              <MenuItem value={baud} key={baud}>
                {baud}
              </MenuItem>
            ))}
          </FieldSelect>
          <Grid size={{ xs: 12 }}>
            <Alert severity="warning">
              Address and baud changes are sent to the valve's current Modbus
              address. The baud rate also applies to the shared Modbus master.
            </Alert>
          </Grid>
        </Grid>
      </DialogForm>
    </CardBox>
  );
};
