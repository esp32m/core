import React from 'react';
import {
  Alert,
  Button,
  Chip,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  FormControlLabel,
  MenuItem,
  Stack,
  Switch,
  TextField,
  Typography,
} from '@mui/material';
import { CardBox } from '@ts-libs/ui-app';
import { NameValueList, useModuleState, useSampleAge } from '../..';
import { useBackendApi } from '../../backend';
import type {
  ThermostatConfig,
  ThermostatProps,
  ThermostatState,
} from './types';

const text = (error: unknown) =>
  error instanceof Error ? error.message : String(error);
const celsius = (value?: number | null) =>
  typeof value === 'number' && Number.isFinite(value)
    ? `${value.toFixed(1)} °C`
    : 'Unavailable';

export default ({ name, title }: ThermostatProps) => {
  const state = useModuleState<ThermostatState>(name);
  const sourceAge = useSampleAge(state?.source.sampledAtMs, name);
  const api = useBackendApi();
  const lock = React.useRef(false);
  const [busy, setBusy] = React.useState(false);
  const [error, setError] = React.useState<string>();
  const [draft, setDraft] = React.useState<ThermostatConfig>();

  const perform = async (operation: () => Promise<unknown>) => {
    if (lock.current) return;
    lock.current = true;
    setBusy(true);
    setError(undefined);
    try {
      await operation();
    } catch (failure) {
      setError(text(failure));
    } finally {
      lock.current = false;
      setBusy(false);
    }
  };

  if (!state) return null;
  const { config, climate, control, sources, source } = state;
  const selected = sources.find(
    (candidate) => candidate.uid === config.sensorUid
  );
  const selectedName = config.sensorUid
    ? selected?.name || config.sensorUid
    : 'Heater native temperature';
  const save = () => {
    if (!draft) return;
    void perform(async () => {
      await api.setConfig(name, draft);
      setDraft(undefined);
    });
  };
  const numberField = (
    key: keyof ThermostatConfig,
    label: string,
    minimum: number,
    maximum: number,
    step = 1,
    scale = 1
  ) =>
    draft && (
      <TextField
        key={key}
        label={label}
        type="number"
        fullWidth
        value={Number(draft[key]) / scale}
        disabled={busy}
        slotProps={{ htmlInput: { min: minimum, max: maximum, step } }}
        onChange={(event) =>
          setDraft({ ...draft, [key]: Number(event.target.value) * scale })
        }
      />
    );

  return (
    <CardBox
      title={title || name}
      action={
        <Chip
          size="small"
          color={control.active ? 'success' : 'default'}
          label={
            !config.enabled
              ? 'Disabled'
              : config.shadow
                ? 'Observe only'
                : climate.mode === 'heat'
                  ? 'Heat'
                  : 'Off'
          }
        />
      }
    >
      <Stack spacing={2}>
        {error && <Alert severity="error">{error}</Alert>}
        {config.shadow && (
          <Alert severity="info">
            Observe only: proposed decisions are shown; heater controls stay
            with their current owner.
          </Alert>
        )}
        {control.inhibited && (
          <Alert severity="warning">
            Automatic control is suspended: {control.reason}. Resume after
            checking the heater and temperature source.
          </Alert>
        )}
        {!source.valid && (
          <Alert severity="warning">
            The selected room temperature is unavailable or stale. Automatic
            restart is inhibited.
          </Alert>
        )}
        <NameValueList
          list={[
            ['Room temperature', celsius(climate.currentTemperature)],
            ['Target', celsius(climate.temperature)],
            ['Temperature source', selectedName],
            [
              'Source age',
              sourceAge < 0
                ? 'Unavailable'
                : `${(sourceAge / 1000).toFixed(1)} s`,
            ],
            ['Heater phase', control.phase],
            ['Action', climate.action || 'Unknown'],
            ['Control status', control.reason],
            ['Backend / strategy', `${control.backend} / ${control.strategy}`],
            ['Last command', control.pending || control.lastCommand || 'None'],
            ['Command result', control.lastResult || 'None'],
            ...(config.shadow
              ? [['Proposed command', control.proposedCommand]]
              : []),
          ]}
        />
        <Stack direction="row" spacing={1} useFlexGap sx={{ flexWrap: 'wrap' }}>
          <Button
            disabled={
              busy || !config.enabled || !control.ready || control.inhibited
            }
            variant={climate.mode === 'heat' ? 'contained' : 'outlined'}
            onClick={() =>
              void perform(() => api.setState(name, { mode: 'heat' }))
            }
          >
            Heat
          </Button>
          <Button
            disabled={busy || !config.enabled}
            variant={climate.mode === 'off' ? 'contained' : 'outlined'}
            onClick={() =>
              void perform(() => api.setState(name, { mode: 'off' }))
            }
          >
            Off
          </Button>
          {control.inhibited && (
            <Button
              disabled={busy || !config.enabled}
              onClick={() =>
                void perform(() => api.setState(name, { resume: true }))
              }
            >
              Resume automatic control
            </Button>
          )}
          <Button disabled={busy} onClick={() => setDraft({ ...config })}>
            Settings
          </Button>
        </Stack>
        <Typography variant="caption" color="text.secondary">
          Heat is thermostat intent. The heater can be starting, cooling or
          stopped while Heat remains selected.
        </Typography>
      </Stack>
      <Dialog
        open={Boolean(draft)}
        onClose={() => !busy && setDraft(undefined)}
        fullWidth
        maxWidth="sm"
      >
        <DialogTitle>Thermostat settings</DialogTitle>
        <DialogContent>
          {draft && (
            <Stack spacing={2} sx={{ mt: 1 }}>
              <FormControlLabel
                label="Enable thermostat"
                control={
                  <Switch
                    disabled={busy}
                    checked={draft.enabled}
                    onChange={(_, checked) =>
                      setDraft({ ...draft, enabled: checked })
                    }
                  />
                }
              />
              <FormControlLabel
                label="Observe only"
                control={
                  <Switch
                    disabled={busy}
                    checked={draft.shadow}
                    onChange={(_, checked) =>
                      setDraft({ ...draft, shadow: checked })
                    }
                  />
                }
              />
              <TextField
                select
                fullWidth
                label="Room temperature sensor"
                value={draft.sensorUid}
                disabled={busy}
                onChange={(event) =>
                  setDraft({ ...draft, sensorUid: event.target.value })
                }
              >
                <MenuItem
                  value=""
                  disabled={!control.nativeTemperatureSupported}
                >
                  Heater native temperature
                </MenuItem>
                {sources.map((candidate) => (
                  <MenuItem
                    key={candidate.uid}
                    value={candidate.uid}
                    disabled={!candidate.enabled}
                  >
                    {candidate.name} ({candidate.device})
                    {candidate.valid ? '' : ' — unavailable'}
                  </MenuItem>
                ))}
                {draft.sensorUid &&
                  !sources.some(
                    (candidate) => candidate.uid === draft.sensorUid
                  ) && (
                    <MenuItem value={draft.sensorUid}>
                      {draft.sensorUid} — missing
                    </MenuItem>
                  )}
              </TextField>
              <TextField
                select
                fullWidth
                label="Control strategy"
                value={draft.strategy}
                disabled={busy}
                onChange={(event) =>
                  setDraft({
                    ...draft,
                    strategy: event.target
                      .value as ThermostatConfig['strategy'],
                  })
                }
              >
                <MenuItem value="automatic">Automatic</MenuItem>
                <MenuItem value="native">Heater thermostat</MenuItem>
                <MenuItem value="power">External power regulation</MenuItem>
                {control.flowSupported && (
                  <MenuItem value="flow">Boiler flow temperature</MenuItem>
                )}
              </TextField>
              {numberField('targetC', 'Target temperature (°C)', -20, 40, 0.5)}
              {numberField(
                'heatHysteresisC',
                'Restart below target by (°C)',
                0.1,
                15,
                0.1
              )}
              {numberField(
                'stopHysteresisC',
                'Stop above target by (°C)',
                0.1,
                15,
                0.1
              )}
              {numberField(
                'proportionalBandC',
                'Power regulation band (°C)',
                0.1,
                30,
                0.1
              )}
              {numberField(
                'highTemperatureC',
                'High temperature shutdown (°C)',
                draft.targetC + 0.1,
                60,
                0.5
              )}
              {control.flowSupported &&
                numberField(
                  'minimumFlowC',
                  'Minimum boiler flow temperature (°C)',
                  0,
                  100,
                  1
                )}
              {control.flowSupported &&
                numberField(
                  'maximumFlowC',
                  'Maximum boiler flow temperature (°C)',
                  0,
                  100,
                  1
                )}
              {!control.flowSupported &&
                numberField(
                  'minimumRunMs',
                  'Minimum run time (minutes)',
                  0,
                  1440,
                  1,
                  60000
                )}
              {!control.flowSupported &&
                numberField(
                  'minimumOffMs',
                  'Minimum off time (minutes)',
                  0,
                  1440,
                  1,
                  60000
                )}
              {!control.flowSupported &&
                numberField(
                  'lowPowerHoldMs',
                  'Low output persistence (seconds)',
                  0,
                  3600,
                  1,
                  1000
                )}
              {numberField(
                'sensorFreshnessMs',
                'Maximum temperature age (seconds)',
                1,
                3600,
                1,
                1000
              )}
              {numberField(
                'feedbackFreshnessMs',
                'Maximum heater feedback age (seconds)',
                1,
                3600,
                1,
                1000
              )}
              {numberField(
                'commandTimeoutMs',
                'Command observation timeout (seconds)',
                1,
                3600,
                1,
                1000
              )}
              {control.fullShutdownSupported && (
                <FormControlLabel
                  label="Full shutdown when satisfied"
                  control={
                    <Switch
                      checked={draft.allowFullShutdown}
                      disabled={busy}
                      onChange={(_, checked) =>
                        setDraft({ ...draft, allowFullShutdown: checked })
                      }
                    />
                  }
                />
              )}
              <Typography variant="caption" color="text.secondary">
                {control.flowSupported
                  ? 'Off requests no room heat; the boiler keeps control of CH enable and hot water.'
                  : 'Disabling automatic control releases its ownership. Off requests the heater’s normal shutdown and preserves its cooling sequence.'}
              </Typography>
            </Stack>
          )}
        </DialogContent>
        <DialogActions>
          <Button disabled={busy} onClick={() => setDraft(undefined)}>
            Cancel
          </Button>
          <Button disabled={busy || !draft} onClick={save}>
            Save
          </Button>
        </DialogActions>
      </Dialog>
    </CardBox>
  );
};
