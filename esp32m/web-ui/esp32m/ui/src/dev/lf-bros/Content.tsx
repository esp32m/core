import React from 'react';
import {
  Alert,
  Box,
  Button,
  Chip,
  Divider,
  Grid,
  Slider,
  Stack,
  ToggleButton,
  ToggleButtonGroup,
  Typography,
} from '@mui/material';
import { CardBox } from '@ts-libs/ui-app';

import * as Backend from '../../backend';
import { NameValueList, useModuleState } from '../..';
import { ControlMode, LfBrosProps, LfBrosState } from './types';

const number = (
  value: number | undefined,
  digits: number,
  unit: string
) =>
  Number.isFinite(value)
    ? `${(value as number).toFixed(digits)}${unit ? ` ${unit}` : ''}`
    : '—';

const onOff = (value: boolean | undefined) =>
  value === undefined ? '—' : value ? 'On' : 'Off';

export default ({ name, title }: LfBrosProps) => {
  const state = useModuleState<LfBrosState>(name);
  const api = Backend.useBackendApi();
  const [busy, setBusy] = React.useState(false);
  const [temperature, setTemperature] = React.useState(18);
  const [power, setPower] = React.useState(2000);

  React.useEffect(() => {
    if (!state || busy) return;
    setTemperature(state.command.temperatureSetpointC);
    setPower(state.command.powerSetpointW);
  }, [
    state?.command.temperatureSetpointC,
    state?.command.powerSetpointW,
    busy,
  ]);

  if (!state) return null;

  const send = async (update: Record<string, unknown>) => {
    setBusy(true);
    try {
      await api.setState(name, update);
    } finally {
      setBusy(false);
    }
  };

  const { command, heater, session, can } = state;
  const runtime = heater.valid;
  const ready = state.connected && session.sourceSelected;
  const standby = heater.valid && heater.state?.code === 1;
  const polrSelected = command.operationMode === 'polr';
  const limits = command.limits;
  const display = heater.physicalDisplay;
  const fault = heater.fault;
  const pump = heater.pump;

  const telemetry = [
    [
      'Heater state',
      heater.state
        ? `${heater.state.name} (${heater.state.code})`
        : 'Waiting for telemetry',
    ],
    ['Ambient temperature', number(heater.temperature?.ambientC, 1, '°C')],
    ['Chamber temperature', number(heater.temperature?.chamberC, 1, '°C')],
    ['Supply voltage', number(heater.electrical?.supplyVoltage, 2, 'V')],
    [
      'Fan',
      runtime
        ? `${number(heater.fan?.speedRpm, 0, 'RPM')} / target ${number(
            heater.fan?.targetRpm,
            0,
            'RPM'
          )}`
        : '—',
    ],
    [
      'Fuel pump',
      runtime
        ? pump?.mode1Pumping
          ? 'On, mode-1 pumping observed (frequency unavailable)'
          : `${onOff(pump?.on)}, ${number(pump?.frequencyHz, 2, 'Hz')}`
        : '—',
    ],
    [
      'Glow plug',
      runtime
        ? `${onOff(heater.glow?.on)}, ${number(
            heater.glow?.dutyPercent,
            1,
            '%'
          )}, ${number(heater.glow?.estimatedPowerW, 1, 'W')}`
        : '—',
    ],
  ];

  const displayValues = [
    ['Control mode', display?.controlModeName || '—'],
    [
      'Temperature setpoint',
      number(display?.temperatureSetpointC, 1, '°C'),
    ],
    ['Power setpoint', number(display?.powerSetpointW, 0, 'W')],
    ['ON/OFF command', display ? display.onOffCommand : '—'],
  ];

  const canDiagnostics = [
    ['Received / application drops', `${can.rxFrames} / ${can.rxDropped}`],
    [
      'Hardware RX maximum pending',
      can.hardwareRxDiagnostics ? can.maxHardwareRxPending : 'Unavailable',
    ],
    [
      'Hardware miss / overrun observations',
      can.hardwareRxDiagnostics
        ? `${can.hardwareRxMissEvents} / ${can.hardwareDataOverrunEvents}`
        : 'Unavailable',
    ],
    [
      'Last error burst HW status',
      can.hardwareRxDiagnostics
        ? `0x${(can.lastErrorBurstHardwareStatus || 0)
            .toString(16)
            .padStart(3, '0')}`
        : 'Unavailable',
    ],
    [
      'Last error burst maximum pending',
      can.hardwareRxDiagnostics
        ? can.lastErrorBurstMaxRxPending
        : 'Unavailable',
    ],
    [
      'CAN errors / bursts',
      `${can.errorEvents} / ${can.errorBursts}`,
    ],
  ];

  return (
    <CardBox
      title={title || name}
      action={
        <Stack direction="row" spacing={1}>
          <Chip
            size="small"
            color={state.connected ? 'success' : 'default'}
            label={state.connected ? 'Connected' : session.stage}
          />
          <Chip
            size="small"
            color={can.state === 'error-active' ? 'success' : 'warning'}
            label={`CAN ${can.state}`}
          />
        </Stack>
      }
    >
      <Stack spacing={2}>
        {fault?.active && (
          <Alert severity="error">
            Heater fault code {fault.displayCode}. A latched fault must be
            acknowledged before the heater can start again.
          </Alert>
        )}

        {heater.stale && (
          <Alert severity="warning">
            Last complete heater telemetry is{' '}
            {number((heater.telemetryAgeMs || 0) / 1000, 1, 's')} old.
            {command.priming
              ? ' Mode-1 pump strokes are disrupting CAN runtime responses; activity is inferred from observed state 23, while the normal Hz field is unavailable.'
              : ' Displayed measurements are the last valid snapshot.'}
          </Alert>
        )}

        {heater.partial && !heater.stale && (
          <Alert severity="info">
            The latest heater state was recovered from an incomplete CAN
            response (chunk mask 0x
            {(heater.chunkMask || 0).toString(16).padStart(6, '0')}). Values
            from missing chunks may still show the preceding complete sample;
            Home Assistant measurements remain unavailable until a complete,
            CRC-checked response arrives.
          </Alert>
        )}

        {polrSelected && (
          <Alert severity="warning">
            {command.operationModeRestorePending
              ? 'Polr stopped; waiting for standby before restoring normal mode.'
              : 'Experimental Polr candidate selected. Equivalence with the display Polr procedure is not yet confirmed; Stop aborts the sequence.'}
          </Alert>
        )}

        <Box>
          <Typography variant="subtitle2" gutterBottom>
            Heater control
          </Typography>
          <Stack direction="row" spacing={1} useFlexGap flexWrap="wrap">
            <Button
              variant={command.on ? 'contained' : 'outlined'}
              color="success"
              disabled={busy || !ready || command.on || polrSelected}
              onClick={() => send({ on: true })}
            >
              Start
            </Button>
            <Button
              variant={!command.on ? 'contained' : 'outlined'}
              color="warning"
              disabled={busy || !ready || !command.on}
              onClick={() => send({ on: false })}
            >
              Stop
            </Button>
            <Button
              variant={command.priming ? 'contained' : 'outlined'}
              color="warning"
              disabled={
                busy || !ready || !standby || command.on || polrSelected
              }
              onClick={() => {
                if (
                  window.confirm(
                    'Start the corrected experimental Polr candidate (mode 1, target 500)? The earlier target-0 test did not match manual Polr. Use only with an empty fuel line; unnecessary pumping can flood the combustion chamber.'
                  )
                )
                  send({ polr: true });
              }}
            >
              Prime fuel line (Polr)
            </Button>
            {fault?.active && heater.state?.code === 31 && (
              <Button
                variant="contained"
                color="error"
                disabled={busy || !ready}
                onClick={() => {
                  if (
                    window.confirm(
                      'Acknowledge the latched heater fault? This restarts the heater controller, matching the physical display fault-clear path. The ESP32 will reconnect automatically.'
                    )
                  )
                    send({ clearFault: true });
                }}
              >
                Clear fault
              </Button>
            )}
          </Stack>
          {!ready && (
            <Typography variant="caption" color="text.secondary">
              Controls become available after the safe CAN session is claimed.
            </Typography>
          )}
        </Box>

        <Divider />

        <Box>
          <Typography variant="subtitle2" gutterBottom>
            ESP master setpoint
          </Typography>
          <ToggleButtonGroup
            exclusive
            size="small"
            value={command.controlMode}
            disabled={busy || polrSelected}
            onChange={(_, mode: ControlMode | null) => {
              if (mode) send({ controlMode: mode });
            }}
          >
            <ToggleButton value="temperature">Temperature</ToggleButton>
            <ToggleButton value="power">Power</ToggleButton>
          </ToggleButtonGroup>

          {command.controlMode === 'temperature' ? (
            <Grid container spacing={2} sx={{ mt: 1 }} alignItems="center">
              <Grid size={{ xs: 12, sm: 3 }}>
                <Typography>{temperature.toFixed(0)} °C</Typography>
              </Grid>
              <Grid size={{ xs: 12, sm: 9 }}>
                <Slider
                  min={limits.minimumTemperatureC}
                  max={limits.maximumTemperatureC}
                  step={1}
                  value={temperature}
                  disabled={busy || polrSelected}
                  valueLabelDisplay="auto"
                  valueLabelFormat={(value) => `${value} °C`}
                  onChange={(_, value) => setTemperature(value as number)}
                  onChangeCommitted={(_, value) =>
                    send({ temperatureSetpointC: value as number })
                  }
                />
              </Grid>
            </Grid>
          ) : (
            <Grid container spacing={2} sx={{ mt: 1 }} alignItems="center">
              <Grid size={{ xs: 12, sm: 3 }}>
                <Typography>{power.toFixed(0)} W</Typography>
              </Grid>
              <Grid size={{ xs: 12, sm: 9 }}>
                <Slider
                  min={limits.minimumPowerW}
                  max={limits.maximumPowerW}
                  step={100}
                  value={power}
                  disabled={busy || polrSelected}
                  valueLabelDisplay="auto"
                  valueLabelFormat={(value) => `${value} W`}
                  onChange={(_, value) => setPower(value as number)}
                  onChangeCommitted={(_, value) =>
                    send({ powerSetpointW: value as number })
                  }
                />
              </Grid>
            </Grid>
          )}
        </Box>

        <Divider />

        <Box>
          <Typography variant="subtitle2">Live heater telemetry</Typography>
          <NameValueList list={telemetry} />
        </Box>

        <Divider />

        <Box>
          <Typography variant="subtitle2">Physical display (read-only)</Typography>
          <Typography variant="caption" color="text.secondary">
            These values belong to the separate display mailbox and may differ
            while the ESP owns heater control.
          </Typography>
          <NameValueList list={displayValues} />
        </Box>

        <Divider />

        <Box>
          <Typography variant="subtitle2">CAN diagnostics</Typography>
          <Typography variant="caption" color="text.secondary">
            Hardware miss and overrun values are observations made in the TWAI
            error ISR. They are distinct from application queue drops.
          </Typography>
          <NameValueList list={canDiagnostics} />
        </Box>
      </Stack>
    </CardBox>
  );
};
