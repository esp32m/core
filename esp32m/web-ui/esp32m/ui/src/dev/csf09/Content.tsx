import { CardBox } from '@ts-libs/ui-app';
import { errorToString } from '@ts-libs/tools';
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
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import { Settings } from '@mui/icons-material';
import { useBackendApi, useSubscribedState, useSampleAge } from '../..';
import { NameValueList } from '../../app';
import {
  DeviceDisabledAlert,
  DeviceDisableSetting,
  useDeviceControl,
} from '../DeviceControl';
import { Name, TConfig, TProps, TState } from './types';

const formatReading = (value: unknown, unit: string) =>
  typeof value === 'number' && Number.isFinite(value)
    ? `${value.toFixed(1)} ${unit}`
    : 'Unavailable';

const validAddress = (address: unknown): address is number =>
  typeof address === 'number' &&
  Number.isInteger(address) &&
  address >= 1 &&
  address <= 247;

const configProblem = (config?: TConfig) => {
  if (!config) return 'Settings are unavailable. Refresh before editing.';
  if (!validAddress(config.address) || config.baud !== 9600)
    return 'Current sensor settings could not be verified. Refresh before editing.';
  if (
    config.communicationError !== undefined &&
    config.communicationError !== 0
  )
    return `The current sensor ID could not be verified (error ${config.communicationError}). Refresh before editing.`;
  if (config.pendingAddress !== undefined)
    return 'The previous ID change is awaiting verification. Refresh after the sensor reconnects; do not repeat the write.';
  return undefined;
};

export const Content = ({
  name: configuredName,
  title = 'CSF09 soil sensor',
  addr,
}: TProps) => {
  const name = configuredName || Name;
  const [state, , live, stateError] = useSubscribedState<TState>(name);
  const age = useSampleAge(state?.[0], name);
  const api = useBackendApi();
  const deviceControl = useDeviceControl(name);
  const active = useRef(false);
  const [busy, setBusy] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [config, setConfig] = useState<TConfig>();
  const [addressInput, setAddressInput] = useState('');
  const [connectInput, setConnectInput] = useState('1');
  const [settingsError, setSettingsError] = useState<string>();
  const [
    ,
    address,
    moisture,
    temperature,
    communicationPending = false,
    lastError = 0,
  ] = Array.isArray(state) ? state : [];
  const pending = communicationPending || config?.pendingAddress !== undefined;
  const fresh =
    live &&
    Number.isFinite(age) &&
    age >= 0 &&
    age <= 60 * 1000 &&
    !pending &&
    !deviceControl.disabled;
  // Keep an open settings dialog reachable while a verified ID change updates
  // telemetry. With no known address, the named module still provides settings.
  if (
    addr !== undefined &&
    address !== undefined &&
    addr !== address &&
    !settingsOpen
  )
    return null;

  const list: Array<[string, string | number]> = [
    ['Moisture', fresh ? formatReading(moisture, '%') : 'Unavailable'],
    ['Temperature', fresh ? formatReading(temperature, '°C') : 'Unavailable'],
  ];
  if (addr === undefined && validAddress(address))
    list.push(['MODBUS address', address]);
  if (lastError) list.push(['Device error', lastError]);

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
    setAddressInput(
      current && validAddress(current.address) ? String(current.address) : ''
    );
    return current;
  };
  const refresh = async () => {
    if (!enter()) return;
    setSettingsOpen(true);
    setSettingsError(undefined);
    setConfig(undefined);
    setAddressInput('');
    try {
      await readConfig();
      await api.getState(name).catch(() => undefined);
    } catch (error) {
      setSettingsError(
        errorToString(error) || 'Could not read CSF09 settings.'
      );
    } finally {
      leave();
    }
  };
  const parsedAddress = Number(addressInput);
  const inputValid = /^\d+$/.test(addressInput) && validAddress(parsedAddress);
  const parsedConnectAddress = Number(connectInput);
  const connectValid =
    /^\d+$/.test(connectInput) && validAddress(parsedConnectAddress);
  const problem = configProblem(config);
  const actionDisabled =
    busy ||
    deviceControl.disabled ||
    deviceControl.busy ||
    !deviceControl.known ||
    !config ||
    pending;
  const settingsDisabled = actionDisabled || !!problem;
  const canSave =
    !settingsDisabled && inputValid && config?.address !== parsedAddress;
  const canConnect = !actionDisabled && connectValid;
  const applyAddress = async (
    kind: 'address' | 'connectAddress',
    requestedAddress: number
  ) => {
    if (!(kind === 'address' ? canSave : canConnect) || !enter()) return;
    setSettingsError(undefined);
    let failure: string | undefined;
    let current: TConfig | undefined;
    try {
      await api.request(
        name,
        'config-set',
        { [kind]: requestedAddress },
        { timeout: kind === 'address' ? 30_000 : 15_000 }
      );
    } catch (error) {
      failure =
        errorToString(error) ||
        (kind === 'address'
          ? 'Could not change the sensor ID.'
          : 'Could not connect to the sensor.');
    }
    try {
      current = await readConfig();
    } catch (error) {
      setConfig(undefined);
      setAddressInput('');
      failure = [failure, `Could not refresh settings: ${errorToString(error)}`]
        .filter(Boolean)
        .join(' ');
    }
    await api.getState(name).catch(() => undefined);
    const issue = failure || configProblem(current);
    if (issue) setSettingsError(issue);
    else if (current?.address !== requestedAddress)
      setSettingsError(
        'The requested ID was not verified. Current settings have been reloaded.'
      );
    else if (kind === 'address') setSettingsOpen(false);
    leave();
  };
  const save = () => applyAddress('address', parsedAddress);
  const connect = () => applyAddress('connectAddress', parsedConnectAddress);

  return (
    <CardBox
      title={title || name}
      action={
        <IconButton
          aria-label={`Configure ${title || name}`}
          disabled={busy}
          onClick={refresh}
        >
          <Settings />
        </IconButton>
      }
    >
      <DeviceDisabledAlert control={deviceControl} />
      {stateError && <Alert severity="warning" sx={{ mb: 2 }}>
        {stateError === 'unsupported-state'
          ? 'This firmware does not provide state for this sensor.'
          : `Sensor state is unavailable: ${stateError}.`}
      </Alert>}
      {pending && (
        <Alert severity="warning">
          The sensor ID change is awaiting verification. Settings are locked;
          refresh after the sensor reconnects.
        </Alert>
      )}
      {!fresh && !deviceControl.disabled && (
        <Alert severity="warning">No recent sensor reading</Alert>
      )}
      <NameValueList list={list} />
      <Dialog
        open={settingsOpen}
        onClose={() => {
          if (!active.current) setSettingsOpen(false);
        }}
        fullWidth
        maxWidth="sm"
      >
        <DialogTitle>Configure {title || name}</DialogTitle>
        {busy && <LinearProgress aria-label="Updating sensor settings" />}
        <DialogContent>
          <DeviceDisableSetting control={deviceControl} busy={busy} />
          <Stack spacing={2} sx={{ mt: 2 }}>
            {(settingsError || (!busy && problem)) && (
              <Alert severity="error">{settingsError || problem}</Alert>
            )}
            {pending && (
              <Alert severity="warning">
                A previous ID change has not been resolved. Refresh to read the
                latest settings; do not repeat the write.
              </Alert>
            )}
            <Typography variant="subtitle1">
              Connect to another sensor
            </Typography>
            <Typography variant="body2" color="text.secondary">
              Select the ID that this ESP32 polls. Connect checks the sensor at
              that ID before using it; it does not change the sensor&apos;s ID.
            </Typography>
            <Typography variant="body2">
              Currently polling ID: {config?.address ?? address ?? 'Unknown'}
            </Typography>
            <TextField
              label="Sensor ID to connect to"
              value={connectInput}
              onChange={(event) => setConnectInput(event.target.value)}
              disabled={actionDisabled}
              error={!connectValid}
              helperText="Integer from 1 to 247; factory ID is 1"
              slotProps={{ htmlInput: { inputMode: 'numeric' } }}
              fullWidth
            />
            <Button disabled={!canConnect} onClick={connect} variant="outlined">
              Connect
            </Button>
            <Typography variant="body2" color="text.secondary">
              To commission factory sensors, connect one unconfigured sensor at
              a time. Connect to ID 1, change its sensor ID to an unused value,
              then disconnect it before connecting the next factory sensor.
            </Typography>
            <Typography variant="subtitle1">Change sensor ID</Typography>
            <TextField
              label="Modbus slave address"
              value={addressInput}
              onChange={(event) => setAddressInput(event.target.value)}
              disabled={settingsDisabled}
              error={!!config && !inputValid}
              helperText="Integer from 1 to 247"
              slotProps={{ htmlInput: { inputMode: 'numeric' } }}
              fullWidth
            />
            <Typography>Baud rate: 9600 (8N1, fixed)</Typography>
            <Typography variant="body2" color="text.secondary">
              Baud rate configuration is not documented for CSF09.
            </Typography>
            <Alert severity="warning">
              Changing the ID writes the sensor configuration. Use an unused ID.
              Ensure only one sensor uses the current ID.
            </Alert>
          </Stack>
        </DialogContent>
        <DialogActions>
          <Button disabled={busy} onClick={refresh}>
            Refresh
          </Button>
          <Button disabled={busy} onClick={() => setSettingsOpen(false)}>
            Cancel
          </Button>
          <Button disabled={!canSave} onClick={save} variant="contained">
            OK
          </Button>
        </DialogActions>
      </Dialog>
    </CardBox>
  );
};
