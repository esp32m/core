import { CardBox } from '@ts-libs/ui-app';
import { errorToString } from '@ts-libs/tools';
import { useRef, useState } from 'react';
import {
  DeviceDisabledAlert,
  DeviceDisableSetting,
  useDeviceControl,
} from '../DeviceControl';
import {
  Accordion,
  AccordionDetails,
  AccordionSummary,
  Typography,
  Alert,
  Button,
  CircularProgress,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  IconButton,
  MenuItem,
  Stack,
  TextField,
} from '@mui/material';
import { ExpandMore, Settings } from '@mui/icons-material';
import { NameValueList } from '../../app';
import { useBackendApi, useModuleState, useSampleAge } from '../..';
import { Name, TConfig, TProps, TState } from './types';

const BaudRates = [1200, 2400, 4800, 9600, 19200];

const UnitNames = [
  '°C',
  '°F',
  'MPa',
  'kPa',
  'Pa',
  'bar',
  'mbar',
  'psi',
  'mH₂O',
  'mmH₂O',
];

const RegisterNames = [
  'Address',
  'Baud code',
  'Unit code',
  'Decimals',
  'Measurement',
  'Range zero',
  'Range full',
  'Zero offset',
];

const formatRegister = (value: number, signed: boolean) => {
  const unsigned = value & 0xffff;
  const hex = `0x${unsigned.toString(16).toUpperCase().padStart(4, '0')}`;
  if (signed && unsigned >= 0x8000)
    return `${unsigned - 0x10000} signed / ${unsigned} (${hex})`;
  return `${unsigned} (${hex})`;
};

const formatCalibrationValue = (value: number) =>
  Number(value.toPrecision(10)).toString();

const ProtocolDiagnostics = ({ registers }: { registers: number[] }) => {
  if (registers.length < RegisterNames.length) return null;
  const list: Array<[string, string]> = RegisterNames.map((name, index) => [
    `R${index} ${name}`,
    formatRegister(registers[index], index >= 4),
  ]);
  return (
    <Accordion>
      <AccordionSummary
        expandIcon={<ExpandMore />}
        id="maqi136-protocol-diagnostics-header"
      >
        <Typography>Protocol diagnostics</Typography>
      </AccordionSummary>
      <AccordionDetails>
        <NameValueList list={list} />
      </AccordionDetails>
    </Accordion>
  );
};

export const Content = ({ title = Name, addr }: TProps) => {
  const state = useModuleState<TState>(Name);
  const age = useSampleAge(state?.[0], Name);
  const api = useBackendApi();
  const deviceControl = useDeviceControl(Name);
  const active = useRef(false);
  const [busy, setBusy] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [config, setConfig] = useState<TConfig>();
  const [address, setAddress] = useState('1');
  const [settingsBaud, setSettingsBaud] = useState(9600);
  const [settingsError, setSettingsError] = useState<string>();
  const [
    ,
    actualAddr,
    unit = 0,
    decimals = 0,
    precision = 3,
    min = 0,
    max = 0,
    value = 0,
    offset = 0,
    registers = [],
    calibrated = false,
    calibrationMultiplier = 0,
    calibrationOffset = 0,
    baud = 9600,
    communicationPending = false,
    lastError = 0,
  ] = state || [];
  const fresh = !!state && age >= 0 && age <= 60 * 1000;
  if (addr && actualAddr && addr != actualAddr) return null;

  const nativeUnit = UnitNames[unit] || `Unknown (${unit})`;
  const underRange = calibrated && registers.length > 4 && registers[4] === 0;
  const list: Array<[string, string | number]> = [
    [
      'Level',
      underRange
        ? `${value.toFixed(precision)} m (under-range)`
        : `${value.toFixed(precision)} m`,
    ],
    ['Range', `${min.toFixed(precision)}–${max.toFixed(precision)} m`],
    ['Native format', `${nativeUnit}, ${decimals} decimals`],
  ];
  if (calibrated) {
    const sign = calibrationOffset < 0 ? '−' : '+';
    list.push([
      'Calibration',
      `${formatCalibrationValue(calibrationMultiplier)} × R4 ${sign} ${formatCalibrationValue(Math.abs(calibrationOffset))} m`,
    ]);
  }
  if (offset) list.push(['Zero offset', `${offset.toFixed(precision)} m`]);
  if (!addr && actualAddr) list.push(['MODBUS address', actualAddr]);
  list.push(['Baud rate', baud]);
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
      Name,
      'config-get',
      {},
      { timeout: 15_000 }
    );
    const current = response.data as TConfig;
    setConfig(current);
    setAddress(String(current.address));
    setSettingsBaud(current.baud);
    return current;
  };
  const openSettings = async () => {
    if (!enter()) return;
    setSettingsOpen(true);
    setSettingsError(undefined);
    setConfig(undefined);
    try {
      await readConfig();
    } catch (error) {
      setSettingsError(errorToString(error) || 'Could not read MAQI settings.');
    } finally {
      leave();
    }
  };
  const parsedAddress = Number(address);
  const addressValid =
    /^\d+$/.test(address) && parsedAddress >= 1 && parsedAddress <= 247;
  const communicationProblem =
    config?.communicationError &&
    `Current R0/R1 settings could not be verified (error ${config.communicationError}).`;
  const configValid =
    !!config &&
    !communicationProblem &&
    config.pendingAddress === undefined &&
    config.pendingBaud === undefined &&
    addressValid &&
    BaudRates.includes(settingsBaud);
  const configChanged =
    config?.address !== parsedAddress || config?.baud !== settingsBaud;
  const saveSettings = async () => {
    if (!configValid || !configChanged || !enter()) return;
    setSettingsError(undefined);
    let failure: string | undefined;
    try {
      await api.request(
        Name,
        'config-set',
        { address: parsedAddress, baud: settingsBaud },
        { timeout: 60_000 }
      );
    } catch (error) {
      failure = errorToString(error) || 'Could not save MAQI settings.';
    }
    try {
      const current = await readConfig();
      if (
        !failure &&
        current.address === parsedAddress &&
        current.baud === settingsBaud &&
        current.pendingAddress === undefined &&
        current.pendingBaud === undefined
      )
        setSettingsOpen(false);
      else if (!failure)
        failure =
          'The requested settings were not verified. Current settings were reloaded.';
    } catch (error) {
      setConfig(undefined);
      failure = [failure, `Could not refresh settings: ${errorToString(error)}`]
        .filter(Boolean)
        .join(' ');
    }
    await api.getState(Name).catch(() => undefined);
    if (failure) setSettingsError(failure);
    leave();
  };
  const settingsDisabled =
    busy ||
    deviceControl.disabled ||
    deviceControl.busy ||
    !config ||
    communicationPending ||
    !!communicationProblem;

  return (
    <CardBox
      title={title}
      action={
        <IconButton
          aria-label={`Configure ${title}`}
          disabled={busy}
          onClick={openSettings}
        >
          <Settings />
        </IconButton>
      }
    >
      <DeviceDisabledAlert control={deviceControl} />
      {communicationPending && (
        <Alert severity="warning">
          Communication change is awaiting verification. Settings are locked
          while the sensor reconnects.
        </Alert>
      )}
      {fresh ? (
        <NameValueList list={list} />
      ) : (
        !deviceControl.disabled && (
          <Alert severity="warning">No recent sensor reading</Alert>
        )
      )}
      {fresh && <ProtocolDiagnostics registers={registers} />}
      <Dialog
        open={settingsOpen}
        onClose={() => {
          if (!busy) setSettingsOpen(false);
        }}
        fullWidth
        maxWidth="sm"
      >
        <DialogTitle>Configure {title}</DialogTitle>
        <DialogContent>
          <DeviceDisableSetting control={deviceControl} busy={busy} />
          <Stack spacing={2} sx={{ mt: 2 }}>
            {busy && (
              <CircularProgress size={24} aria-label="Updating settings" />
            )}
            {(settingsError || communicationProblem) && (
              <Alert severity="error">
                {settingsError || communicationProblem}
              </Alert>
            )}
            {(communicationPending || config?.pendingAddress !== undefined) && (
              <Alert severity="warning">
                A previous communication change has not yet been resolved.
                Refresh after the sensor reconnects; do not repeat the write.
              </Alert>
            )}
            <TextField
              label="Modbus slave address"
              value={address}
              onChange={(event) => setAddress(event.target.value)}
              disabled={settingsDisabled}
              error={!!config && !addressValid}
              helperText="Integer from 1 to 247"
              slotProps={{ htmlInput: { inputMode: 'numeric' } }}
              fullWidth
            />
            <TextField
              select
              label="Baud rate"
              value={settingsBaud}
              onChange={(event) => setSettingsBaud(Number(event.target.value))}
              disabled={settingsDisabled}
              fullWidth
            >
              {BaudRates.map((value) => (
                <MenuItem key={value} value={value}>
                  {value}
                </MenuItem>
              ))}
            </TextField>
            <Alert severity="warning">
              These settings are written to the sensor. Changing baud rate also
              changes the shared Modbus bus speed, so every device on this bus
              must use the same rate.
            </Alert>
          </Stack>
        </DialogContent>
        <DialogActions>
          <Button disabled={busy} onClick={() => setSettingsOpen(false)}>
            Cancel
          </Button>
          <Button
            variant="contained"
            disabled={!configValid || !configChanged || settingsDisabled}
            onClick={saveSettings}
          >
            OK
          </Button>
        </DialogActions>
      </Dialog>
    </CardBox>
  );
};
