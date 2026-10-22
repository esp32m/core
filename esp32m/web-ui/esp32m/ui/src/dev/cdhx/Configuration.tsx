import React from 'react';
import {
  Alert,
  Box,
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  Divider,
  MenuItem,
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import { NameValueList, useSampleAge } from '../..';
import { CdhxConfigUpdate, CdhxMode, CdhxService, CdhxState } from './types';

type Props = {
  name: string;
  state: CdhxState;
  busy: boolean;
  error?: string;
  onClose: () => void;
  onConfigSave: (config: CdhxConfigUpdate) => Promise<void>;
};

type ServiceDraft = Record<keyof CdhxService, string>;

type Draft = {
  mode: Exclude<CdhxMode, 'unknown'>;
  temperature: string;
  power: string;
  service: ServiceDraft;
};

const serviceNumberFields = [
  {
    key: 'minimumPumpHz',
    label: 'Minimum pump setting (Hz)',
    min: 0.8,
    max: 2.5,
    step: 0.1,
    digits: 1,
    unit: 'Hz',
  },
  {
    key: 'maximumPumpHz',
    label: 'Maximum pump setting (Hz)',
    min: 2.2,
    max: 8,
    step: 0.1,
    digits: 1,
    unit: 'Hz',
  },
  {
    key: 'minimumFanRpm',
    label: 'Minimum fan speed (RPM)',
    min: 1350,
    max: 2600,
    step: 10,
    digits: 0,
    unit: 'RPM',
  },
  {
    key: 'maximumFanRpm',
    label: 'Maximum fan speed (RPM)',
    min: 3500,
    max: 5000,
    step: 10,
    digits: 0,
    unit: 'RPM',
  },
  {
    key: 'glowPowerLevel',
    label: 'Glow-power level',
    min: 1,
    max: 6,
    step: 1,
    digits: 0,
    unit: '',
  },
] as const;

const validServiceNumber = (
  input: string,
  min: number,
  max: number,
  step: number
) => {
  const value = Number(input);
  return (
    input.trim() !== '' &&
    Number.isFinite(value) &&
    value >= min &&
    value <= max &&
    Math.abs(value / step - Math.round(value / step)) < 1e-8
  );
};

const serviceRows = (service: CdhxService | undefined) => [
  [
    'Minimum / maximum pump',
    `${format(service?.minimumPumpHz, 1, 'Hz')} / ${format(service?.maximumPumpHz, 1, 'Hz')}`,
  ],
  [
    'Minimum / maximum fan',
    `${format(service?.minimumFanRpm, 0, 'RPM')} / ${format(service?.maximumFanRpm, 0, 'RPM')}`,
  ],
  ['Configured voltage', format(service?.configuredVoltageV, 0, 'V')],
  ['Hall count (advertised)', format(service?.hallCount)],
  ['Glow-power level', format(service?.glowPowerLevel)],
];

const format = (input: number | null | undefined, digits = 0, unit = '') =>
  typeof input === 'number' && Number.isFinite(input)
    ? `${input.toFixed(digits)}${unit ? ` ${unit}` : ''}`
    : '—';

const modeLabel = (mode: CdhxMode | null) =>
  mode === 'temperature'
    ? 'Temperature'
    : mode === 'power'
      ? 'Fixed power'
      : 'Unknown';

const initialDraft = (state: CdhxState): Draft => {
  const { configuration, control, display } = state;
  const mode = configuration?.mode ?? display.mode;
  const temperature =
    configuration?.temperatureSetpointC ??
    control.temperatureOverrideC ??
    display.temperatureSetpointC;
  const power =
    configuration?.fixedPowerSettingRaw ??
    control.fixedPowerOverrideRaw ??
    (display.mode === 'power' ? display.requestedRaw : undefined);
  return {
    mode: mode === 'power' ? 'power' : 'temperature',
    temperature: temperature == null ? '' : String(temperature),
    power: power == null ? '' : String(power),
    service: {
      minimumPumpHz:
        configuration?.service.minimumPumpHz == null
          ? ''
          : String(configuration.service.minimumPumpHz),
      maximumPumpHz:
        configuration?.service.maximumPumpHz == null
          ? ''
          : String(configuration.service.maximumPumpHz),
      minimumFanRpm:
        configuration?.service.minimumFanRpm == null
          ? ''
          : String(configuration.service.minimumFanRpm),
      maximumFanRpm:
        configuration?.service.maximumFanRpm == null
          ? ''
          : String(configuration.service.maximumFanRpm),
      configuredVoltageV:
        configuration?.service.configuredVoltageV == null
          ? ''
          : String(configuration.service.configuredVoltageV),
      hallCount:
        configuration?.service.hallCount == null
          ? ''
          : String(configuration.service.hallCount),
      glowPowerLevel:
        configuration?.service.glowPowerLevel == null
          ? ''
          : String(configuration.service.glowPowerLevel),
    },
  };
};

export default ({ name, state, busy, error, onClose, onConfigSave }: Props) => {
  const displayAge = useSampleAge(state.display.sampledAtMs > 0 ? state.display.sampledAtMs : -1, name);
  // The parent mounts this component only while the dialog is open. Polling
  // updates eligibility and observations without replacing an edited draft.
  const [draft, setDraft] = React.useState(() => initialDraft(state));
  const { configuration, display, control } = state;
  const pending = Boolean(control.pendingCommand);
  const temperature = Number(draft.temperature);
  const power = Number(draft.power);
  const temperatureRangeKnown =
    Number.isInteger(display.minimumTemperatureC) &&
    Number.isInteger(display.maximumTemperatureC) &&
    display.minimumTemperatureC >= 0 &&
    display.maximumTemperatureC <= 127 &&
    display.minimumTemperatureC < display.maximumTemperatureC;
  const temperatureValid =
    temperatureRangeKnown &&
    draft.temperature.trim() !== '' &&
    Number.isInteger(temperature) &&
    temperature >= display.minimumTemperatureC &&
    temperature <= display.maximumTemperatureC;
  const powerMinimum = control.fixedPowerMinimumRaw;
  const powerMaximum = control.fixedPowerMaximumRaw;
  const powerRangeKnown =
    powerMinimum !== null &&
    powerMaximum !== null &&
    Number.isInteger(powerMinimum) &&
    Number.isInteger(powerMaximum) &&
    powerMinimum <= powerMaximum;
  const powerValid =
    powerRangeKnown &&
    draft.power.trim() !== '' &&
    Number.isInteger(power) &&
    power >= (powerMinimum as number) &&
    power <= (powerMaximum as number);
  const changesMode = draft.mode !== (configuration?.mode ?? display.mode);
  const modeAvailable =
    (!changesMode || configuration?.modeChangeAvailable) &&
    (draft.mode === 'temperature'
      ? configuration?.ambientAvailable
      : configuration?.powerAvailable);
  const applyDisabled =
    busy ||
    pending ||
    !configuration?.canApply ||
    !modeAvailable ||
    !(draft.mode === 'temperature' ? temperatureValid : powerValid);
  const service =
    configuration?.serviceAuthority || display.requestHex
      ? configuration?.service
      : undefined;
  const serviceFieldsDisabled =
    busy || !configuration?.serviceWritable || !configuration.serviceAuthority;
  const serviceNumbersValid = serviceNumberFields.every(
    ({ key, min, max, step }) =>
      validServiceNumber(draft.service[key], min, max, step)
  );
  const pumpOrderValid =
    Number(draft.service.minimumPumpHz) <= Number(draft.service.maximumPumpHz);
  const serviceValid =
    serviceNumbersValid &&
    pumpOrderValid &&
    ['12', '24'].includes(draft.service.configuredVoltageV) &&
    ['1', '2'].includes(draft.service.hallCount);
  const serviceSaveDisabled =
    serviceFieldsDisabled ||
    pending ||
    !configuration?.serviceCanApply ||
    !serviceValid;

  const editService = (key: keyof ServiceDraft, value: string) =>
    setDraft({ ...draft, service: { ...draft.service, [key]: value } });

  const displayServiceNote = (
    key: keyof CdhxService,
    digits = 0,
    unit = ''
  ) => {
    if (!configuration?.serviceAuthority || !display.requestHex) return null;
    const stock = configuration.stockService?.[key];
    const effective = configuration.service[key];
    if (
      typeof stock !== 'number' ||
      !Number.isFinite(stock) ||
      typeof effective !== 'number' ||
      !Number.isFinite(effective) ||
      Math.abs(stock - effective) < 1e-8
    )
      return null;
    return (
      <Box component="span" sx={{ display: 'block' }}>
        Display: {format(stock, digits, unit)}
        {!display.valid &&
          ` (last request, ${format(displayAge < 0 ? undefined : displayAge / 1000, 1, 's')} old)`}
      </Box>
    );
  };

  const saveService = () => {
    if (serviceSaveDisabled || !configuration) return;
    void onConfigSave({
      service: {
        minimumPumpHz: Number(draft.service.minimumPumpHz),
        maximumPumpHz: Number(draft.service.maximumPumpHz),
        minimumFanRpm: Number(draft.service.minimumFanRpm),
        maximumFanRpm: Number(draft.service.maximumFanRpm),
        configuredVoltageV: Number(draft.service.configuredVoltageV),
        hallCount: Number(draft.service.hallCount),
        glowPowerLevel: Number(draft.service.glowPowerLevel),
      },
    });
  };

  const apply = () => {
    if (applyDisabled || !configuration) return;
    void onConfigSave({
      operating:
        draft.mode === 'temperature'
          ? { mode: 'temperature', temperatureSetpointC: temperature }
          : { mode: 'power', fixedPowerSettingRaw: power },
    });
  };

  return (
    <Dialog
      open
      onClose={() => {
        if (!busy) onClose();
      }}
      fullWidth
      maxWidth="sm"
      aria-labelledby={`${name}-configuration-title`}
    >
      <DialogTitle id={`${name}-configuration-title`}>
        Configuration
      </DialogTitle>
      <DialogContent>
        <Stack spacing={2}>
          {error && <Alert severity="error">{error}</Alert>}
          {!configuration && (
            <Alert severity="info">
              This firmware does not expose configuration controls.
            </Alert>
          )}
          {configuration?.reason && (
            <Alert severity="warning">{configuration.reason}</Alert>
          )}
          <Box>
            <Typography variant="subtitle2" gutterBottom>
              Operating mode and setpoint
            </Typography>
            <Stack spacing={2}>
              <TextField
                select
                label="Operating mode"
                size="small"
                value={draft.mode}
                disabled={busy || !configuration}
                onChange={(event) =>
                  setDraft({
                    ...draft,
                    mode: event.target.value as Draft['mode'],
                  })
                }
              >
                <MenuItem value="temperature">Temperature</MenuItem>
                <MenuItem value="power">Fixed power</MenuItem>
              </TextField>
              {draft.mode === 'temperature' ? (
                <TextField
                  label="Temperature setpoint (°C)"
                  size="small"
                  type="number"
                  value={draft.temperature}
                  disabled={busy || !configuration || !temperatureRangeKnown}
                  onChange={(event) =>
                    setDraft({ ...draft, temperature: event.target.value })
                  }
                  slotProps={{
                    htmlInput: {
                      min: display.minimumTemperatureC,
                      max: display.maximumTemperatureC,
                      step: 1,
                    },
                  }}
                  helperText={
                    temperatureRangeKnown
                      ? `${display.minimumTemperatureC}–${display.maximumTemperatureC} °C, whole degrees`
                      : 'Waiting for valid display temperature limits'
                  }
                />
              ) : (
                <TextField
                  label="Fixed-power setting (raw)"
                  size="small"
                  type="number"
                  value={draft.power}
                  disabled={busy || !configuration || !powerRangeKnown}
                  onChange={(event) =>
                    setDraft({ ...draft, power: event.target.value })
                  }
                  slotProps={{
                    htmlInput: {
                      min: powerMinimum ?? undefined,
                      max: powerMaximum ?? undefined,
                      step: 1,
                    },
                  }}
                  helperText={
                    powerRangeKnown
                      ? `${powerMinimum}–${powerMaximum}, integer raw value`
                      : 'No qualified raw setting range configured'
                  }
                />
              )}
              {draft.mode === 'temperature' &&
                configuration &&
                !configuration.ambientAvailable && (
                  <Alert severity="info">
                    Temperature control needs fresh ambient readings from the
                    physical display in temperature mode.
                  </Alert>
                )}
              {draft.mode === 'power' &&
                configuration &&
                !configuration.powerAvailable && (
                  <Alert severity="info">
                    Fixed-power control requires a qualified range matching the
                    current service settings.
                  </Alert>
                )}
              {changesMode &&
                configuration &&
                !configuration.modeChangeAvailable && (
                  <Alert severity="info">
                    Mode changes require a stopped heater with the fan and pump
                    off.
                  </Alert>
                )}
              <Stack
                direction="row"
                spacing={1}
                useFlexGap
                sx={{ flexWrap: 'wrap' }}
              >
                <Button
                  variant="contained"
                  disabled={applyDisabled}
                  onClick={apply}
                >
                  Save mode and setpoint
                </Button>
              </Stack>
            </Stack>
          </Box>
          <NameValueList
            list={[
              ['Physical display mode', modeLabel(display.mode)],
              [
                'ESP mode and setpoint',
                configuration?.mode
                  ? `${modeLabel(configuration.mode)} / ${format(configuration.value, 0, configuration.mode === 'temperature' ? '°C' : 'raw')}`
                  : 'Unavailable',
              ],
              [
                'ESP submitted mode / setting',
                configuration
                  ? `${modeLabel(configuration.submittedMode)} / ${format(configuration.submittedValue)}`
                  : '—',
              ],
              ['Configuration phase', configuration?.phase || '—'],
              [
                'ESP configuration status',
                configuration?.authority
                  ? configuration.active
                    ? 'Applying saved settings'
                    : 'Saved; waiting for eligible requests'
                  : 'Waiting for configuration',
              ],
              ['Command result', control.lastCommandResult || '—'],
            ]}
          />
          <Typography variant="caption" color="text.secondary">
            Saved mode and setpoint do not expire and survive ESP restarts.
            Physical display changes do not replace them. Submitted values
            describe an attempted request; heater feedback does not acknowledge
            the mode.
          </Typography>
          <Divider />
          <Box>
            <Typography variant="subtitle2" gutterBottom>
              Service parameters (1688 menu)
            </Typography>
            <Typography variant="body2" color="text.secondary">
              {configuration?.serviceWritable
                ? 'Saved on the ESP and substituted into heater requests. These settings survive ESP restarts; changing them on the physical display does not replace the saved values. Saving new values requires a stopped heater.'
                : 'Read from the physical display request. This firmware does not expose service editing.'}
            </Typography>
            {configuration?.serviceReason && (
              <Alert severity="info" sx={{ mt: 1 }}>
                {configuration.serviceReason}
              </Alert>
            )}
            {configuration?.serviceWritable && (
              <Stack spacing={2} sx={{ mt: 2, mb: 2 }}>
                {serviceNumberFields.map(
                  ({ key, label, min, max, step, digits, unit }) => (
                    <TextField
                      key={key}
                      label={label}
                      size="small"
                      type="number"
                      value={draft.service[key]}
                      disabled={serviceFieldsDisabled}
                      onChange={(event) => editService(key, event.target.value)}
                      error={
                        draft.service[key].trim() !== '' &&
                        !validServiceNumber(draft.service[key], min, max, step)
                      }
                      slotProps={{ htmlInput: { min, max, step } }}
                      helperText={
                        <>
                          {`${min}–${max}, step ${step}`}
                          {key === 'glowPowerLevel' && '; level, not watts'}
                          {displayServiceNote(key, digits, unit)}
                        </>
                      }
                    />
                  )
                )}
                {!pumpOrderValid && (
                  <Alert severity="warning">
                    Minimum pump setting must not exceed the maximum.
                  </Alert>
                )}
                <TextField
                  select
                  label="Configured voltage"
                  size="small"
                  value={draft.service.configuredVoltageV}
                  disabled={serviceFieldsDisabled}
                  onChange={(event) =>
                    editService('configuredVoltageV', event.target.value)
                  }
                  helperText={displayServiceNote('configuredVoltageV', 0, 'V')}
                >
                  <MenuItem value="12">12 V</MenuItem>
                  <MenuItem value="24">24 V</MenuItem>
                </TextField>
                <TextField
                  select
                  label="Hall count (advertised)"
                  size="small"
                  value={draft.service.hallCount}
                  disabled={serviceFieldsDisabled}
                  onChange={(event) =>
                    editService('hallCount', event.target.value)
                  }
                  helperText={
                    <>
                      H45214A0 ignores this advertised value and uses Hall count
                      1.
                      {displayServiceNote('hallCount')}
                    </>
                  }
                >
                  <MenuItem value="1">1</MenuItem>
                  <MenuItem value="2">2</MenuItem>
                </TextField>
                <Button
                  variant="contained"
                  disabled={serviceSaveDisabled}
                  onClick={saveService}
                >
                  Save service settings
                </Button>
              </Stack>
            )}
            {!configuration?.serviceWritable && (
              <>
                <NameValueList list={serviceRows(service)} />
                <Typography variant="caption" color="text.secondary">
                  {display.requestHex
                    ? `Display request age: ${format(displayAge < 0 ? undefined : displayAge / 1000, 1, 's')}${display.valid ? '' : ' (last valid snapshot)'}`
                    : 'Waiting for a valid display request.'}
                </Typography>
              </>
            )}
          </Box>
        </Stack>
      </DialogContent>
      <DialogActions>
        <Button disabled={busy} onClick={onClose}>
          Close
        </Button>
      </DialogActions>
    </Dialog>
  );
};
