import { DeviceDisabledAlert, DeviceDisableSetting, useDeviceControl } from '../DeviceControl';
import { useRef, useState } from 'react';
import {
  Alert,
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  IconButton,
  LinearProgress,
  MenuItem,
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import { Settings } from '@mui/icons-material';
import { CardBox } from '@ts-libs/ui-app';
import { errorToString } from '@ts-libs/tools';
import { NameValueList } from '../../app';
import { useBackendApi, useSubscribedState, useSampleAge } from '../..';
import { Name } from './types';
import type { TConfig, TProps, TSettings, TState } from './types';
import {
  BaudRates,
  changesFor,
  configProblem,
  fresh,
  inputsFor,
  pending,
  validInputs,
} from './values';

export const Content = ({ name = Name, title = Name }: TProps) => {
  const settingsTitleId = `${encodeURIComponent(name)}-settings-title`;
  const [state, , live] = useSubscribedState<TState>(name);
  const age = useSampleAge(state?.sampledAtMs, name);
  const api = useBackendApi();
  const deviceControl = useDeviceControl(name);
  const active = useRef(false);
  const [busy, setBusy] = useState(false);
  const [open, setOpen] = useState(false);
  const [config, setConfig] = useState<TConfig>();
  const [input, setInput] = useState(() => inputsFor());
  const [error, setError] = useState<string>();

  const enter = () => {
    if (active.current) return false;
    active.current = true;
    setBusy(true);
    return true;
  };
  const leave = () => {
    active.current = false;
    setBusy(false);
  };
  const readConfig = async () => {
    const response = await api.request(
      name,
      'config-get',
      {},
      { timeout: 15_000 }
    );
    const current = response.data as TConfig | undefined;
    setConfig(current);
    setInput(inputsFor(current));
    return current;
  };
  const refresh = async () => {
    if (!enter()) return;
    setOpen(true);
    setError(undefined);
    setConfig(undefined);
    setInput(inputsFor());
    try {
      await readConfig();
      await api.getState(name).catch(() => undefined);
    } catch (e) {
      setError(errorToString(e) || 'Could not read sensor settings.');
    } finally {
      leave();
    }
  };

  const valid = validInputs(input);
  const problem = configProblem(config);
  const changes = config ? changesFor(config, input) : {};
  const canSave =
    !busy && !deviceControl.disabled && !deviceControl.busy &&
    !problem &&
    !state?.communicationPending &&
    Object.values(valid).every(Boolean) &&
    Object.keys(changes).length > 0;
  const save = async () => {
    if (!canSave || !enter()) return;
    setError(undefined);
    let failure: string | undefined;
    let current: TConfig | undefined;
    try {
      // Send only edited fields. Never replay diagnostic or pending fields.
      await api.request(name, 'config-set', changes, { timeout: 60_000 });
    } catch (e) {
      failure = errorToString(e) || 'Could not save sensor settings.';
    }
    try {
      // A partial update or a lost reply must reload actual settings rather
      // than leave old form values ready for an accidental repeated write.
      current = await readConfig();
    } catch (e) {
      setConfig(undefined);
      setInput(inputsFor());
      failure = [failure, `Could not refresh settings: ${errorToString(e)}`]
        .filter(Boolean)
        .join(' ');
    }
    await api.getState(name).catch(() => undefined);
    const issue = failure || configProblem(current);
    if (issue) setError(issue);
    else if (
      (Object.keys(changes) as Array<keyof TSettings>).some(
        (key) => current?.[key] !== changes[key]
      )
    )
      setError(
        'The requested settings were not verified. Current settings have been reloaded.'
      );
    else setOpen(false);
    leave();
  };

  const available = live && fresh(state, age);
  const waiting = state?.communicationPending || pending(config);
  const status = available
    ? state?.alarm
      ? 'Leak detected'
      : 'No leak detected'
    : state && live
      ? 'No recent sensor reading'
      : 'Waiting for sensor data';
  const list: Array<[string, string | number]> = [
    ['Raw sensing value', available ? state!.analog! : 'Unavailable'],
    ['Modbus address', state?.address ?? 'Unknown'],
    ['Baud rate', state?.baud ?? 'Unknown'],
    [
      'Last reading',
      age >= 0
        ? `${Math.floor(age / 1000)} s ago`
        : 'Not received',
    ],
  ];
  if (state?.lastError) list.push(['Device error', state.lastError]);
  const disabled = deviceControl.disabled || deviceControl.busy || busy || !!problem || !!state?.communicationPending;
  const change = (key: keyof TSettings, value: string) =>
    setInput((previous) => ({ ...previous, [key]: value }));

  return (
    <CardBox
      title={title}
      action={
        <IconButton
          aria-label={`Configure ${title}`}
          disabled={busy}
          onClick={refresh}
        >
          <Settings />
        </IconButton>
      }
    >
      <DeviceDisabledAlert control={deviceControl} />
      <Stack spacing={2}>
        <Alert
          severity={
            available ? (state?.alarm ? 'error' : 'success') : 'warning'
          }
        >
          {status}
        </Alert>
        {state?.communicationPending && (
          <Alert severity="warning">
            Communication change is awaiting verification. Settings are locked
            while the sensor reconnects.
          </Alert>
        )}
        <NameValueList list={list} />
      </Stack>
      <Dialog
        open={open}
        onClose={() => {
          if (!active.current) setOpen(false);
        }}
        fullWidth
        maxWidth="sm"
        aria-labelledby={settingsTitleId}
      >
        <DialogTitle id={settingsTitleId}>Configure {title}</DialogTitle>
        {busy && <LinearProgress aria-label="Updating sensor settings" />}
        <DialogContent>
          <DeviceDisableSetting control={deviceControl} busy={busy} />
          <Stack spacing={3} sx={{ pt: 1 }}>
            {(error || (!busy && problem)) && (
              <Alert severity="error">{error || problem}</Alert>
            )}
            {waiting && (
              <Alert severity="warning">
                Waiting for communication verification.
                {pending(config) &&
                  ` Requested address: ${config?.pendingAddress ?? 'unknown'}, baud: ${config?.pendingBaud ?? 'unknown'}.`}{' '}
                Refresh to read the latest settings.
              </Alert>
            )}
            <TextField
              label="Modbus slave address"
              value={input.address}
              onChange={(event) => change('address', event.target.value)}
              disabled={disabled}
              error={!!config && !valid.address}
              helperText="Integer from 1 to 247"
              slotProps={{ htmlInput: { inputMode: 'numeric' } }}
              fullWidth
            />
            <TextField
              label="Baud rate"
              value={input.baud}
              onChange={(event) => change('baud', event.target.value)}
              disabled={disabled}
              error={!!config && !valid.baud}
              select
              fullWidth
            >
              {BaudRates.map((baud) => (
                <MenuItem key={baud} value={String(baud)}>
                  {baud}
                </MenuItem>
              ))}
            </TextField>
            <TextField
              label="Sensitivity"
              value={input.sensitivity}
              onChange={(event) => change('sensitivity', event.target.value)}
              disabled={disabled}
              error={!!config && !valid.sensitivity}
              helperText="200–500; higher values are more sensitive"
              slotProps={{ htmlInput: { inputMode: 'numeric' } }}
              fullWidth
            />
            <TextField
              label="Alarm delay (s)"
              value={input.alarmDelay}
              onChange={(event) => change('alarmDelay', event.target.value)}
              disabled={disabled}
              error={!!config && !valid.alarmDelay}
              helperText="0–65535 seconds; 0 means no delay"
              slotProps={{ htmlInput: { inputMode: 'numeric' } }}
              fullWidth
            />
            <Alert severity="info">
              Changing baud rate also changes the shared Modbus bus. Configure
              one device at a time and keep the other devices at a compatible
              baud rate.
            </Alert>
            <Typography variant="caption" color="text.secondary">
              Settings are applied one at a time. If a later change fails,
              earlier verified changes remain applied.
            </Typography>
          </Stack>
        </DialogContent>
        <DialogActions sx={{ flexWrap: 'wrap' }}>
          <Button onClick={refresh} disabled={busy}>
            Refresh
          </Button>
          <Button onClick={() => setOpen(false)} disabled={busy}>
            Cancel
          </Button>
          <Button onClick={save} disabled={!canSave} variant="contained">
            Save
          </Button>
        </DialogActions>
      </Dialog>
    </CardBox>
  );
};
