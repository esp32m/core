import { DeviceDisabledAlert, DeviceDisableSetting, useDeviceControl } from '../DeviceControl';
import { useState } from 'react';
import {
  Alert,
  Button,
  Checkbox,
  CircularProgress,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  FormControlLabel,
  IconButton,
  MenuItem,
  Stack,
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableRow,
  TextField,
  Typography,
} from '@mui/material';
import { Settings } from '@mui/icons-material';
import { CardBox } from '@ts-libs/ui-app';
import { useSnackApi } from '@ts-libs/ui-snack';
import { NameValueList } from '../../app';
import { useBackendApi, useModuleState, useSampleAge } from '../..';
import {
  Name,
  TConfig,
  THistory,
  THistoryBank,
  TProps,
  TRtcSync,
  TState,
} from './types';

const BaudRates = [1200, 2400, 4800, 9600];
const HistoryBanks: Array<[THistoryBank, string]> = [
  ['hourly', 'Hourly (48 entries)'],
  ['daily', 'Daily (31 entries)'],
  ['monthly', 'Monthly (24 entries)'],
  ['yearly', 'Yearly (12 entries)'],
];
const RtcSyncLabels: Record<TRtcSync, string> = {
  unknown: 'Not checked yet',
  invalid: 'Invalid calendar; synchronization pending',
  valid: 'Valid calendar',
  'waiting-for-time': 'Waiting for valid ESP32 time',
  'sync-failed': 'Synchronization failed; will retry',
  synced: 'Synchronized with ESP32 time',
  'read-failed': 'Clock read failed; will retry',
};

const number = (
  value: number | null | undefined,
  digits: number,
  unit: string
) =>
  typeof value === 'number' && Number.isFinite(value)
    ? `${value.toFixed(digits)} ${unit}`
    : 'Unavailable';

const calendar = (value: string | null) =>
  value?.replace('T', ' ') || 'Unavailable';

const statusText = (status: number) => {
  const flags = [];
  if (status & 0x04) flags.push('Startup');
  if (status & 0x20) flags.push('Internal temperature ≤ 0 °C');
  if (status & 0x40) flags.push('Low monitored voltage');
  if (status & ~0x64) flags.push('Unknown flags');
  const hex = `0x${status.toString(16).toUpperCase().padStart(2, '0')}`;
  return `${flags.length ? flags.join('; ') : 'No flags'} (${hex})`;
};

export const Content = ({ name = Name, title = Name, addr }: TProps) => {
  const state = useModuleState<TState>(name);
  const age = useSampleAge(state?.sampledAtMs, name);
  const rtcAge = useSampleAge(state?.rtcSampledAtMs, name);
  const api = useBackendApi();
  const deviceControl = useDeviceControl(name);
  const snack = useSnackApi();
  const [busy, setBusy] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [address, setAddress] = useState('1');
  const [baud, setBaud] = useState(9600);
  const [confirmedConfig, setConfirmedConfig] = useState(false);
  const [configLoaded, setConfigLoaded] = useState(false);
  const [configError, setConfigError] = useState<string>();
  const [historyOpen, setHistoryOpen] = useState(false);
  const [historyBank, setHistoryBank] = useState<THistoryBank>('hourly');
  const [history, setHistory] = useState<THistory>();
  const [historyError, setHistoryError] = useState<string>();
  const [historyLoading, setHistoryLoading] = useState(false);
  const [historyReadAt, setHistoryReadAt] = useState<string>();
  const [totalOpen, setTotalOpen] = useState(false);
  const [newTotal, setNewTotal] = useState('');
  const [confirmTotal, setConfirmTotal] = useState(false);
  const [totalError, setTotalError] = useState<string>();

  if (!state || (addr !== undefined && addr !== state.address)) return null;
  const stale = age < 0 || age > 60_000;
  const rtcStale = rtcAge < 0 || rtcAge > 120_000;
  const parsedAddress = Number(address);
  const addressValid =
    /^\d+$/.test(address) &&
    Number.isInteger(parsedAddress) &&
    parsedAddress >= 1 &&
    parsedAddress <= 247 &&
    parsedAddress !== 235;
  const parsedTotal = Number(newTotal);
  const totalValid =
    /^\d+(\.\d{1,4})?$/.test(newTotal) &&
    Number.isFinite(parsedTotal) &&
    parsedTotal >= 0 &&
    parsedTotal <= 9999.9999;
  const changing = parsedAddress !== state.address || baud !== state.baud;
  const list: Array<[string, string | number]> = [
    ['Total', number(stale ? null : state.total, 8, 'm³')],
    ['Flow', number(stale ? null : state.flow, 5, 'm³/h')],
    [
      'Meter RTC',
      rtcStale
        ? 'Unavailable'
        : state.rtcValid
          ? calendar(state.rtc)
          : 'Invalid calendar',
    ],
    ['RTC synchronization', RtcSyncLabels[state.rtcSync] || 'Unknown'],
    [
      'Status',
      stale || state.status === undefined
        ? 'Unavailable'
        : statusText(state.status),
    ],
    ['Internal temperature', number(stale ? null : state.temperature, 1, '°C')],
    ['Monitored battery/supply', number(stale ? null : state.voltage, 3, 'V')],
    [
      'Boot count',
      stale || state.bootCount === undefined ? 'Unavailable' : state.bootCount,
    ],
    ['Modbus address', state.address],
    ['Baud rate', state.baud],
  ];

  const openSettings = async () => {
    setAddress(String(state.address));
    setBaud(state.baud);
    setConfirmedConfig(false);
    setConfigLoaded(false);
    setConfigError(undefined);
    setSettingsOpen(true);
    setBusy(true);
    try {
      const response = await api.getConfig(name);
      const config: TConfig = response.data;
      setAddress(String(config.address));
      setBaud(config.baud);
      setConfigLoaded(true);
    } catch (e) {
      setConfigError(
        'Could not read the connection settings. Close and retry.'
      );
      snack.error(e);
    } finally {
      setBusy(false);
    }
  };
  const saveSettings = async () => {
    if (deviceControl.disabled || deviceControl.busy) return;
    if (
      !addressValid ||
      !BaudRates.includes(baud) ||
      !confirmedConfig ||
      !configLoaded ||
      !changing ||
      busy
    )
      return;
    setBusy(true);
    setConfigError(undefined);
    try {
      await api.request(
        name,
        'config-set',
        {
          address: parsedAddress,
          baud,
          confirm: true,
        },
        { timeout: 45_000 }
      );
      setSettingsOpen(false);
      await api.getState(name);
    } catch (e) {
      setConfigError(
        'The change could not be verified. Check the device state before retrying.'
      );
      snack.error(e);
    } finally {
      setBusy(false);
    }
  };
  const loadHistory = async (bank: THistoryBank) => {
    if (historyLoading || busy) return;
    setHistoryLoading(true);
    setHistoryError(undefined);
    setHistory(undefined);
    try {
      const response = await api.request(
        name,
        'history',
        { bank },
        { timeout: 30_000 }
      );
      setHistory(response.data as THistory);
      setHistoryReadAt(new Date().toLocaleString());
    } catch (e) {
      setHistoryError(
        'Could not read snapshots from the meter. You can retry.'
      );
      snack.error(e);
    } finally {
      setHistoryLoading(false);
    }
  };
  const changeTotal = async () => {
    if (!totalValid || !confirmTotal || busy) return;
    setBusy(true);
    setTotalError(undefined);
    try {
      await api.request(
        name,
        'set-total',
        // Keep the entered decimal exact across JSON and the ESP32 parser.
        { total: newTotal, confirm: true },
        { timeout: 45_000 }
      );
      setConfirmTotal(false);
      setTotalOpen(false);
      await api.getState(name);
    } catch (e) {
      setTotalError(
        'The new total could not be verified. Check the current reading before retrying.'
      );
      snack.error(e);
    } finally {
      setBusy(false);
    }
  };

  return (
    <CardBox
      title={title}
      action={
        <IconButton
          aria-label="Configure SM10"
          onClick={openSettings}
          disabled={busy || historyLoading}
        >
          <Settings />
        </IconButton>
      }
    >
      <DeviceDisabledAlert control={deviceControl} />
      {stale && (
        <Alert severity="warning">
          No recent meter response. Readings are unavailable.
        </Alert>
      )}
      {state.lastError && <Alert severity="warning">{state.lastError}</Alert>}
      <NameValueList list={list} />
      <Typography variant="caption" component="p" sx={{ mt: 1 }}>
        RTC uses ESP32 local time
        {rtcAge >= 0
          ? ` (sampled ${Math.floor(rtcAge / 1000)} s ago)`
          : ''}
        . Temperature is internal; voltage is the meter’s monitored supply, not
        the external RS485 supply.
      </Typography>
      <Stack
        direction="row"
        spacing={1}
        useFlexGap
        sx={{ mt: 2, flexWrap: 'wrap' }}
      >
        <Button
          variant="outlined"
          disabled={busy || historyLoading || deviceControl.disabled || deviceControl.busy}
          onClick={() => {
            setHistoryOpen(true);
            void loadHistory(historyBank);
          }}
        >
          History snapshots
        </Button>
        <Button
          variant="outlined"
          disabled={busy || historyLoading || deviceControl.disabled || deviceControl.busy}
          onClick={() => {
            setNewTotal('');
            setTotalError(undefined);
            setTotalOpen(true);
          }}
        >
          Change total
        </Button>
      </Stack>

      <Dialog
        open={settingsOpen}
        onClose={() => {
          if (!busy) setSettingsOpen(false);
        }}
        fullWidth
        maxWidth="sm"
        aria-labelledby="sm10-settings-title"
      >
        <DialogTitle id="sm10-settings-title">Configure {title}</DialogTitle>
        <DialogContent>
          <DeviceDisableSetting control={deviceControl} busy={busy} />
          <Stack spacing={2} sx={{ mt: 1 }}>
            <TextField
              label="Modbus slave ID"
              value={address}
              onChange={(event) => setAddress(event.target.value)}
              disabled={busy}
              error={!addressValid}
              helperText="1–247, excluding 235 (reserved by this meter’s protocol)"
              slotProps={{ htmlInput: { inputMode: 'numeric' } }}
              fullWidth
            />
            <TextField
              select
              label="Baud rate"
              value={baud}
              onChange={(event) => setBaud(Number(event.target.value))}
              disabled={busy}
              fullWidth
            >
              {BaudRates.map((value) => (
                <MenuItem key={value} value={value}>
                  {value}
                </MenuItem>
              ))}
            </TextField>
            <Alert severity="warning">
              Make changes with no water flowing. Changing the ID or baud rate
              also rounds the stored total down to 0.1 L, losing less than 0.1 L
              of its fine reading. Choose an unused ID on a shared bus.
            </Alert>
            <FormControlLabel
              control={
                <Checkbox
                  checked={confirmedConfig}
                  onChange={(_, checked) => setConfirmedConfig(checked)}
                  disabled={busy}
                />
              }
              label="I understand and want to update the meter settings."
            />
            {configError && <Alert severity="error">{configError}</Alert>}
          </Stack>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setSettingsOpen(false)} disabled={busy}>
            Cancel
          </Button>
          <Button
            variant="contained"
            onClick={saveSettings}
            disabled={
              deviceControl.disabled || deviceControl.busy || busy ||
              !configLoaded ||
              !addressValid ||
              !BaudRates.includes(baud) ||
              !confirmedConfig ||
              !changing
            }
          >
            {busy ? 'Please wait…' : 'Save settings'}
          </Button>
        </DialogActions>
      </Dialog>

      <Dialog
        open={historyOpen}
        onClose={() => {
          if (!historyLoading) setHistoryOpen(false);
        }}
        fullWidth
        maxWidth="sm"
        aria-labelledby="sm10-history-title"
      >
        <DialogTitle id="sm10-history-title">
          {title} history snapshots
        </DialogTitle>
        <DialogContent>
          <Stack spacing={2} sx={{ mt: 1 }}>
            <TextField
              select
              label="History bank"
              value={historyBank}
              disabled={historyLoading}
              onChange={(event) => {
                setHistoryBank(event.target.value as THistoryBank);
                setHistory(undefined);
                setHistoryError(undefined);
              }}
              fullWidth
            >
              {HistoryBanks.map(([value, label]) => (
                <MenuItem key={value} value={value}>
                  {label}
                </MenuItem>
              ))}
            </TextField>
            <Typography variant="body2">
              Cumulative totals, newest first. The meter does not provide entry
              dates or a valid-entry count. Zero can mean an empty slot or a
              real reading; clock changes and restarts can leave gaps.
            </Typography>
            {historyLoading && (
              <Stack
                direction="row"
                spacing={2}
                sx={{ alignItems: 'center' }}
                role="status"
              >
                <CircularProgress size={20} />
                <Typography>Reading meter snapshots…</Typography>
              </Stack>
            )}
            {historyError && <Alert severity="error">{historyError}</Alert>}
            {history && (
              <>
                <Typography variant="caption">
                  Read on demand: {historyReadAt}
                  {history.rtc ? `. Meter RTC: ${calendar(history.rtc)}` : ''}
                </Typography>
                <Table
                  size="small"
                  aria-label={`${history.bank} cumulative snapshots`}
                >
                  <TableHead>
                    <TableRow>
                      <TableCell>Slot</TableCell>
                      <TableCell align="right">Cumulative total</TableCell>
                    </TableRow>
                  </TableHead>
                  <TableBody>
                    {history.values.map((_, index, values) => {
                      const slot = values.length - index;
                      return (
                        <TableRow key={slot}>
                          <TableCell>
                            {slot}
                            {index === 0
                              ? ' (newest)'
                              : slot === 1
                                ? ' (oldest)'
                                : ''}
                          </TableCell>
                          <TableCell align="right">
                            {number(values[slot - 1], 4, 'm³')}
                          </TableCell>
                        </TableRow>
                      );
                    })}
                  </TableBody>
                </Table>
              </>
            )}
          </Stack>
        </DialogContent>
        <DialogActions>
          <Button
            disabled={historyLoading}
            onClick={() => setHistoryOpen(false)}
          >
            Close
          </Button>
          <Button
            disabled={historyLoading || busy || deviceControl.disabled || deviceControl.busy}
            onClick={() => loadHistory(historyBank)}
          >
            {history ? 'Refresh snapshots' : 'Load snapshots'}
          </Button>
        </DialogActions>
      </Dialog>

      <Dialog
        open={totalOpen && !confirmTotal}
        onClose={() => {
          if (!busy) setTotalOpen(false);
        }}
        fullWidth
        maxWidth="sm"
        aria-labelledby="sm10-total-title"
      >
        <DialogTitle id="sm10-total-title">Change {title} total</DialogTitle>
        <DialogContent>
          <Stack spacing={2} sx={{ mt: 1 }}>
            <Typography>
              Current total: {number(stale ? null : state.total, 8, 'm³')}
            </Typography>
            <TextField
              autoFocus
              label="New cumulative total (m³)"
              value={newTotal}
              onChange={(event) => setNewTotal(event.target.value)}
              error={!!newTotal && !totalValid}
              helperText="0–9999.9999 m³; up to 4 decimal places (0.1 L)."
              slotProps={{ htmlInput: { inputMode: 'decimal' } }}
              fullWidth
            />
            <Alert severity="warning">
              Stop water flow first. This replaces the meter’s stored total and
              clears its finer fraction. Existing history is retained and may
              show a discontinuity.
            </Alert>
          </Stack>
        </DialogContent>
        <DialogActions>
          <Button onClick={() => setTotalOpen(false)}>Cancel</Button>
          <Button
            variant="contained"
            disabled={!totalValid || busy}
            onClick={() => setConfirmTotal(true)}
          >
            Review change
          </Button>
        </DialogActions>
      </Dialog>

      <Dialog
        open={confirmTotal}
        onClose={() => {
          if (!busy) setConfirmTotal(false);
        }}
        fullWidth
        maxWidth="sm"
        aria-labelledby="sm10-confirm-total-title"
      >
        <DialogTitle id="sm10-confirm-total-title">
          Confirm total replacement
        </DialogTitle>
        <DialogContent>
          <Stack spacing={2}>
            <Typography>
              Replace {title}’s current total of{' '}
              {number(stale ? null : state.total, 8, 'm³')} with{' '}
              <strong>{number(parsedTotal, 4, 'm³')}</strong>?
            </Typography>
            <Alert severity="warning">
              This writes the new total to the meter. It cannot restore the
              discarded fine fraction. Confirm only while no water is flowing.
            </Alert>
            {totalError && <Alert severity="error">{totalError}</Alert>}
          </Stack>
        </DialogContent>
        <DialogActions>
          <Button disabled={busy} onClick={() => setConfirmTotal(false)}>
            Back
          </Button>
          <Button
            color="warning"
            variant="contained"
            disabled={busy || !totalValid || deviceControl.disabled || deviceControl.busy}
            onClick={changeTotal}
          >
            {busy ? 'Updating meter…' : 'Confirm change total'}
          </Button>
        </DialogActions>
      </Dialog>
    </CardBox>
  );
};
