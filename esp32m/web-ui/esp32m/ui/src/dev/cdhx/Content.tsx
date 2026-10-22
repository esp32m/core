import React from 'react';
import {
  Alert,
  Box,
  Button,
  Chip,
  Divider,
  Stack,
  Typography,
} from '@mui/material';
import { PlayArrow, Stop } from '@mui/icons-material';
import { CardBox } from '@ts-libs/ui-app';
import { NameValueList, useModuleState } from '../..';
import { useBackendApi } from '../../backend';
import type {
  CdhxAction,
  CdhxConfigUpdate,
  CdhxProps,
  CdhxState,
} from './types';
import Configuration from './Configuration';
import Diagnostics from './Diagnostics';
import Display from './Display';
import { namedCode, value } from './format';

const errorText = (error: unknown) =>
  error instanceof Error ? error.message : String(error);

export default ({ name, title }: CdhxProps) => {
  const state = useModuleState<CdhxState>(name);
  const api = useBackendApi();
  const active = React.useRef(false);
  const [busy, setBusy] = React.useState(false);
  const [error, setError] = React.useState<string>();
  const [dialog, setDialog] = React.useState<
    'configuration' | 'diagnostics' | 'display'
  >();

  if (!state) return null;

  const perform = async (operation: () => Promise<unknown>) => {
    if (active.current) return;
    active.current = true;
    setBusy(true);
    setError(undefined);
    try {
      await operation();
    } catch (failure) {
      setError(errorText(failure));
    } finally {
      active.current = false;
      setBusy(false);
    }
  };

  const send = (action: CdhxAction) =>
    perform(() => api.setState(name, action));
  const saveConfig = (config: CdhxConfigUpdate) =>
    perform(() => api.setConfig(name, config));

  const { bypass, display, heater, control } = state;
  const ready =
    state.connected &&
    state.bridgeActive &&
    display.valid &&
    heater.valid &&
    control.available;
  const pending = Boolean(control.pendingCommand);
  const disabled = busy || pending || !ready;
  const startAllowed =
    heater.state === 0 && (heater.status === 0 || heater.status === 1);
  const stopAllowed =
    heater.state !== undefined && heater.state >= 1 && heater.state <= 5;
  const stopOnly = heater.state !== 0;

  const telemetry = [
    ['Heater state', namedCode(heater.state, heater.stateName)],
    ['Supply voltage', value(heater.supplyVoltageV, 2, 'V')],
    [
      'Fan speed / voltage',
      `${value(heater.fanRpm, 0, 'RPM')} / ${value(heater.fanVoltageV, 2, 'V')}`,
    ],
    [
      'Body temperature',
      heater.status === 10
        ? `Sensor fault (reported raw: ${value(heater.bodyTemperatureC)})`
        : value(heater.bodyTemperatureC, 0, '°C'),
    ],
    [
      'Glow voltage / current',
      `${value(heater.glowVoltageV, 2, 'V')} / ${value(heater.glowCurrentA, 2, 'A')}`,
    ],
    ['Pump setting', value(heater.pumpSettingHz, 1, 'Hz')],
  ];

  return (
    <CardBox
      title={title || state.model || name}
      action={
        <Stack direction="row" spacing={1}>
          <Chip
            size="small"
            color={state.connected ? 'success' : 'default'}
            label={state.connected ? 'Connected' : 'Waiting for frames'}
          />
          <Chip
            size="small"
            color={state.bridgeActive ? 'success' : 'warning'}
            label={
              bypass.supported && bypass.active
                ? 'Stock bypass'
                : state.bridgeActive
                  ? 'Bridge active'
                  : 'Bridge stopped'
            }
          />
        </Stack>
      }
    >
      <Stack spacing={2}>
        {error && <Alert severity="error">{error}</Alert>}
        {state.fault && <Alert severity="error">{state.fault}</Alert>}
        {heater.status !== undefined && heater.status >= 2 && (
          <Alert severity={heater.valid ? 'error' : 'warning'}>
            {heater.valid ? 'Heater fault: ' : 'Last reported heater fault: '}
            {namedCode(heater.status, heater.statusName)}
            {!heater.valid && ' (response stale)'}
          </Alert>
        )}
        {!ready && (
          <Alert severity="warning">
            Controls require an active bridge, fresh valid frames, and normal
            temperature or fixed-power operation on the physical display.
            Measurements below may show the last valid snapshot.
          </Alert>
        )}
        <Box>
          <Typography variant="subtitle2">Heater telemetry</Typography>
          <NameValueList list={telemetry} />
          <Typography variant="caption" color="text.secondary">
            Pump setting is a protocol value, not measured fuel flow.
          </Typography>
        </Box>
        <Box>
          <Typography variant="caption" color="text.secondary">
            The active bridge applies saved ESP mode, setpoint, and service
            settings on display requests. These settings take priority over
            physical-display settings and survive ESP restarts.
          </Typography>
          {bypass.supported && bypass.active && (
            <Typography
              variant="caption"
              component="div"
              color="text.secondary"
            >
              {bypass.requested
                ? 'Stock display and heater are connected directly. Resume interposer waits for valid stock exchanges before applying saved ESP settings.'
                : state.fault
                  ? 'Stock bypass is active after a bridge fault. Reset bridge clears the fault before interception can resume.'
                  : `Stock bypass is active while the interposer checks communication (${bypass.qualifiedPairs}/3 valid exchanges).`}
            </Typography>
          )}
        </Box>
        <Divider />
        <Stack direction="row" spacing={1} useFlexGap sx={{ flexWrap: 'wrap' }}>
          <Button
            variant="contained"
            color={stopOnly ? 'error' : 'primary'}
            startIcon={stopOnly ? <Stop /> : <PlayArrow />}
            aria-pressed={stopAllowed}
            disabled={disabled || !(stopOnly ? stopAllowed : startAllowed)}
            onClick={() => void send({ on: !stopOnly })}
          >
            {stopOnly ? 'Stop' : 'Start'}
          </Button>
          <Button
            variant="outlined"
            disabled={busy}
            onClick={() => setDialog('configuration')}
          >
            Configuration
          </Button>
          <Button variant="outlined" onClick={() => setDialog('diagnostics')}>
            Diagnostics
          </Button>
          <Button variant="outlined" onClick={() => setDialog('display')}>
            Display
          </Button>
          {bypass.supported && (
            <Button
              variant="outlined"
              disabled={busy}
              onClick={() => void send({ bypass: !bypass.requested })}
            >
              {bypass.requested ? 'Resume interposer' : 'Bypass interposer'}
            </Button>
          )}
          {state.fault && (
            <Button
              variant="outlined"
              disabled={busy}
              onClick={() => void send({ resetBridge: true })}
            >
              Reset bridge
            </Button>
          )}
        </Stack>
      </Stack>
      {dialog === 'configuration' && (
        <Configuration
          name={name}
          state={state}
          busy={busy}
          error={error}
          onClose={() => setDialog(undefined)}
          onConfigSave={saveConfig}
        />
      )}
      {dialog === 'diagnostics' && (
        <Diagnostics
          name={name}
          state={state}
          onClose={() => setDialog(undefined)}
        />
      )}
      {dialog === 'display' && (
        <Display
          name={name}
          state={state}
          onClose={() => setDialog(undefined)}
        />
      )}
    </CardBox>
  );
};
