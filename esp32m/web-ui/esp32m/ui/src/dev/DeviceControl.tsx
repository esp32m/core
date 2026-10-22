import { useEffect, useRef, useState } from 'react';
import { Alert, Checkbox, FormControlLabel, Stack, Typography } from '@mui/material';
import { errorToString } from '@ts-libs/tools';
import { useBackendApi, useSubscribedState } from '../backend';

type DeviceState = { _disabled: boolean };

// This endpoint only reads local Device metadata, including for drivers whose
// config-get normally reads hardware or whose state-get returns a legacy array.
export const useDeviceControl = (name: string) => {
  const api = useBackendApi();
  const [state, refresh, live] = useSubscribedState<DeviceState>(name, { view: 'device' });
  const [confirmed, setConfirmed] = useState<DeviceState>();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string>();
  const pending = useRef(false);
  useEffect(() => {
    if (!pending.current) setConfirmed(state);
  }, [state]);
  const setDisabled = async (disabled: boolean) => {
    if (pending.current) return;
    pending.current = true;
    setBusy(true);
    setError(undefined);
    try {
      const response = await api.request(name, 'config-set', { _disabled: disabled }, { timeout: 60_000 });
      if (response.data?._disabled !== disabled)
        throw new Error('Device setting was not confirmed. Refresh before retrying.');
      setConfirmed(response.data as DeviceState);
      await refresh();
    } catch (e) {
      setError(errorToString(e) || 'Could not update device setting.');
      // A lost response is not proof that the setting failed. Read back only.
      try { setConfirmed(await refresh()); } catch { /* leave the error visible */ }
    } finally {
      pending.current = false;
      setBusy(false);
    }
  };
  return { disabled: confirmed?._disabled === true, known: !!confirmed && live, busy, error, setDisabled };
};

type Control = ReturnType<typeof useDeviceControl>;

export const DeviceDisabledAlert = ({ control }: { control: Control }) =>
  control.disabled ? <Alert severity="info" sx={{ mb: 2 }}>
    This device is disabled in settings. Polling and bus commands are paused; displayed readings may be old.
  </Alert> : null;

export const DeviceDisableSetting = ({ control, busy = false }: { control: Control; busy?: boolean }) => (
  <Stack spacing={1} sx={{ mb: 2 }}>
    <FormControlLabel label="Disable device" control={
      <Checkbox checked={control.disabled} disabled={busy || control.busy || !control.known}
        onChange={(_, checked) => void control.setDisabled(checked)} />
    } />
    <Typography variant="caption" color="text.secondary">
      Applies immediately and is saved on the ESP32. Pauses this driver after its current operation finishes. Does not stop or power off the physical device.
    </Typography>
    {control.error && <Alert severity="error">{control.error}</Alert>}
  </Stack>
);
