import { useEffect, useRef, useState } from 'react';
import {
  Alert,
  Button,
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
import { useTranslation } from '@ts-libs/ui-i18n';
import { errorToString } from '@ts-libs/tools';
import { useBackendApi } from '../../backend';
import { formatBytes } from '../../utils';
import {
  convertQuota,
  MaxAgeDays,
  PercentUnit,
  quotaBaseBytes,
  quotaPreview,
  settingsConfig,
  trimsHistory,
} from './api';
import { LogConfig, LogInfo, LogState, Name } from './types';

export function SettingsDialog({
  info,
  config,
  state,
  onClose,
  onSaved,
}: {
  info: LogInfo;
  config: LogConfig;
  state?: LogState;
  onClose: VoidFunction;
  onSaved: VoidFunction;
}) {
  const api = useBackendApi();
  const { t } = useTranslation();
  const [enabled, setEnabled] = useState(config.enabled);
  const percentage =
    !!info.supportsQuotaPercent && (config.quotaPercent || 0) > 0;
  const [quota, setQuota] = useState(
    String(percentage ? config.quotaPercent : config.quotaBytes / 1024)
  );
  const [unit, setUnit] = useState(percentage ? PercentUnit : 1024);
  const [age, setAge] = useState(String(config.maxAgeDays));
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');
  const active = useRef(true);
  const inFlight = useRef(false);
  const preview = quotaPreview(quota, unit, info, state);
  const next = settingsConfig(enabled, quota, unit, age, info, state);
  const insufficientSpace =
    !!preview && preview.effectiveQuotaBytes < info.minQuotaBytes;

  useEffect(() => {
    active.current = true;
    return () => {
      active.current = false;
    };
  }, []);

  const save = async () => {
    if (!next || inFlight.current) return;
    inFlight.current = true;
    setSaving(true);
    setError('');
    try {
      await api.setConfig(Name, next);
      if (active.current) onSaved();
    } catch (e) {
      if (active.current)
        setError(errorToString(e) || 'Could not save log settings');
    } finally {
      inFlight.current = false;
      if (active.current) setSaving(false);
    }
  };

  return (
    <Dialog
      open
      onClose={saving ? undefined : onClose}
      fullWidth
      maxWidth="sm"
      aria-labelledby="logs-settings-title"
    >
      <DialogTitle id="logs-settings-title">
        {t('Log storage settings')}
      </DialogTitle>
      <DialogContent>
        <Stack spacing={2} sx={{ pt: 1 }}>
          {error && <Alert severity="error">{error}</Alert>}
          <FormControlLabel
            control={
              <Switch
                checked={enabled}
                onChange={(_, checked) => setEnabled(checked)}
                disabled={saving}
              />
            }
            label={t('Store logs on device')}
          />
          <Typography variant="body2">
            {t('Disabling capture keeps stored logs. Retention still applies.')}
          </Typography>
          <Stack direction={{ xs: 'column', sm: 'row' }} spacing={2}>
            <TextField
              label={t('Storage quota')}
              type="number"
              value={quota}
              onChange={(event) => setQuota(event.target.value)}
              disabled={saving}
              fullWidth
              slotProps={{
                htmlInput: {
                  min: unit === PercentUnit ? 1 : info.minQuotaBytes / unit,
                  max: unit === PercentUnit ? 100 : info.maxQuotaBytes / unit,
                  step: unit === PercentUnit ? 1 : 'any',
                },
              }}
            />
            <TextField
              select
              label={t('Unit')}
              value={unit}
              onChange={(event) => {
                const nextUnit = Number(event.target.value);
                setQuota(
                  convertQuota(
                    quota,
                    unit,
                    nextUnit,
                    quotaBaseBytes(info, state)
                  )
                );
                setUnit(nextUnit);
              }}
              disabled={saving}
              sx={{ minWidth: 170 }}
            >
              <MenuItem value={1024}>KiB</MenuItem>
              <MenuItem value={1048576}>MiB</MenuItem>
              {info.supportsQuotaPercent && (
                <MenuItem value={PercentUnit}>{t('% of free space')}</MenuItem>
              )}
            </TextField>
          </Stack>
          <Typography variant="caption">
            {t('Quota range')}:{' '}
            {unit === PercentUnit
              ? '1–100%'
              : `${formatBytes(info.minQuotaBytes)} – ${formatBytes(info.maxQuotaBytes)}`}
          </Typography>
          {preview && (
            <Typography variant="body2">
              {t('Calculated quota')}: {formatBytes(preview.quotaBytes)}
              {preview.effectiveQuotaBytes < preview.quotaBytes && (
                <>
                  {' '}
                  · {t('After reserve')}:{' '}
                  {formatBytes(preview.effectiveQuotaBytes)}
                </>
              )}
            </Typography>
          )}
          {unit === PercentUnit && (
            <Typography variant="body2">
              {t(
                'Free space is measured as if existing log files were reclaimed. Other app data and the filesystem reserve stay protected.'
              )}
            </Typography>
          )}
          <TextField
            label={t('Maximum age (days)')}
            type="number"
            value={age}
            onChange={(event) => setAge(event.target.value)}
            disabled={saving}
            helperText={t(
              '0 disables age expiry; the byte quota still applies.'
            )}
            slotProps={{ htmlInput: { min: 0, max: MaxAgeDays, step: 1 } }}
            fullWidth
          />
          {insufficientSpace && (
            <Alert severity="warning">
              {t(
                'Not enough space for the minimum log quota after the filesystem reserve.'
              )}
            </Alert>
          )}
          {!next && !insufficientSpace && (
            <Alert severity="warning">
              {t(
                unit === PercentUnit
                  ? 'Choose a whole percentage from 1 to 100 and an age from 0 to 3650 whole days.'
                  : 'Choose a quota within the displayed range and an age from 0 to 3650 whole days.'
              )}
            </Alert>
          )}
          {next && trimsHistory(config, next) && (
            <Alert severity="warning">
              {t(
                'Saving a smaller quota or shorter age may permanently remove older logs.'
              )}
            </Alert>
          )}
        </Stack>
      </DialogContent>
      <DialogActions>
        <Button disabled={saving} onClick={onClose}>
          {t('Cancel')}
        </Button>
        <Button disabled={saving || !next} onClick={save}>
          {t('OK')}
        </Button>
      </DialogActions>
    </Dialog>
  );
}
