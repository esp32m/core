import {
  DeviceDisabledAlert,
  DeviceDisableSetting,
  useDeviceControl,
} from '../DeviceControl';
import { useEffect, useRef, useState } from 'react';
import {
  Alert,
  Box,
  Button,
  CircularProgress,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  IconButton,
  MenuItem,
  Stack,
  Table,
  TableBody,
  TableCell,
  TableContainer,
  TableHead,
  TableRow,
  TextField,
  Tooltip,
  Typography,
} from '@mui/material';
import { Edit, PlayArrow, Refresh, Settings, Stop } from '@mui/icons-material';
import { CardBox } from '@ts-libs/ui-app';
import { MessageBox, useMessageBox } from '@ts-libs/ui-base';
import { useSnackApi } from '@ts-libs/ui-snack';
import { NameValueList } from '../../app';
import { useBackendApi, useModuleState, useSampleAger } from '../..';
import {
  Name,
  TConfig,
  TFaults,
  TParameter,
  TParameters,
  TProps,
  TState,
  TWriteResult,
} from './types';
import {
  BaudRates,
  FaultRegisters,
  Groups,
  canEdit,
  decodeParameter,
  decodeParameters,
  dialogReadWarning,
  editable,
  encodeInput,
  faultText,
  fresh,
  hasRaw,
  parameterInput,
  parameterText,
  rawText,
  registerError,
  reading,
} from './values';

const errorText = (error: unknown) =>
  error instanceof Error ? error.message : String(error);

export const Content = ({ name = Name, title = 'BFS pump', addr }: TProps) => {
  const rawState = useModuleState<TState>(name);
  const ageOf = useSampleAger(name);
  const timed = (channel: TState['status']) => ({ ...channel, age: ageOf(channel.sampledAtMs) });
  const state = rawState && { ...rawState,
    pressureSetpoint: timed(rawState.pressureSetpoint), pressure: timed(rawState.pressure),
    frequencySetpoint: timed(rawState.frequencySetpoint), frequency: timed(rawState.frequency),
    voltage: timed(rawState.voltage), current: timed(rawState.current),
    temperature: timed(rawState.temperature), status: timed(rawState.status) };
  const api = useBackendApi();
  const deviceControl = useDeviceControl(name);
  const snack = useSnackApi();
  const latestState = useRef(state);
  latestState.current = state;
  const active = useRef(false);
  const [busy, setBusy] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [config, setConfig] = useState<TConfig>();
  const [address, setAddress] = useState('1');
  const [baud, setBaud] = useState(9600);
  const [settingsError, setSettingsError] = useState<string>();
  const [faultsOpen, setFaultsOpen] = useState(false);
  const [faults, setFaults] = useState<TFaults>();
  const [faultError, setFaultError] = useState<string>();
  const [parametersOpen, setParametersOpen] = useState(false);
  const [parameters, setParameters] = useState<TParameter[]>(
    decodeParameters([])
  );
  const [parameterWarning, setParameterWarning] = useState<string>();
  const [parameterError, setParameterError] = useState<string>();
  const [selected, setSelected] = useState<TParameter>();
  const [input, setInput] = useState('');
  const [editError, setEditError] = useState<string>();
  const cancelRefresh = useRef(false);
  const [progress, setProgress] = useState<{ done: number; total: number }>();
  useEffect(
    () => () => {
      cancelRefresh.current = true;
    },
    []
  );

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
  const run = async (running: boolean) => {
    if (!enter()) return false;
    let refreshed = false;
    try {
      const status = latestState.current?.status;
      if (
        running &&
        (!fresh(status) || status?.raw === undefined || status.raw & 1)
      )
        throw new Error('Read a current STOP status before starting the pump.');
      const response = await api.request(
        name,
        'run',
        { running, ...(running ? { confirm: true } : {}) },
        { timeout: 15_000 }
      );
      await api.getState(name);
      refreshed = true;
      const result = response.data as TWriteResult;
      if (!result?.verified)
        throw new Error(
          `${running ? 'RUN' : 'STOP'} was not verified.${result?.writeError !== undefined ? ` Write error: ${result.writeError}.` : ''}${result?.readError !== undefined ? ` Status error: ${result.readError}.` : ''}`
        );
      return true;
    } catch (error) {
      // Read back an uncertain action; never repeat an action automatically.
      if (!refreshed) await api.getState(name).catch(() => undefined);
      throw error;
    } finally {
      leave();
    }
  };
  const { messageBoxProps, open: confirm } = useMessageBox({
    start: {
      title: `Start ${title}?`,
      content: 'The pump may start immediately.',
      actions: [{ name: 'ok', onClick: () => run(true) }, 'cancel'],
    },
  });

  const openSettings = async () => {
    if (!enter()) return;
    setSettingsError(undefined);
    setConfig(undefined);
    setSettingsOpen(true);
    try {
      const response = await api.request(
        name,
        'config-get',
        {},
        { timeout: 15_000 }
      );
      const current = response.data as TConfig;
      setConfig(current);
      setAddress(String(current.address));
      setBaud(current.baud);
    } catch (error) {
      setSettingsError(errorText(error));
    } finally {
      leave();
    }
  };
  const parsedAddress = Number(address);
  const addressValid =
    /^\d+$/.test(address) && parsedAddress >= 1 && parsedAddress <= 247;
  const configValid = !!config && addressValid && BaudRates.includes(baud);
  const configChanged =
    config?.address !== parsedAddress || config?.baud !== baud;
  const saveSettings = async () => {
    if (!configValid || !configChanged || !enter()) return;
    setSettingsError(undefined);
    try {
      await api.request(
        name,
        'config-set',
        { address: parsedAddress, baud },
        { timeout: 45_000 }
      );
      setSettingsOpen(false);
      await api.getState(name);
    } catch (error) {
      setSettingsError(errorText(error));
      const response = await api
        .request(name, 'config-get', {}, { timeout: 15_000 })
        .catch(() => undefined);
      if (response?.data) {
        setConfig(response.data);
        setAddress(String(response.data.address));
        setBaud(response.data.baud);
      } else setConfig(undefined);
    } finally {
      leave();
    }
  };
  const loadFaults = async () => {
    if (!enter()) return;
    setFaultsOpen(true);
    setFaults(undefined);
    setFaultError(undefined);
    try {
      const response = await api.request(
        name,
        'faults',
        {},
        { timeout: 60_000 }
      );
      setFaults(response.data as TFaults);
    } catch (error) {
      setFaultError(errorText(error));
    } finally {
      leave();
    }
  };
  const loadParameters = async () => {
    if (!enter()) return;
    setParametersOpen(true);
    setParameters((current) =>
      current.map((p) => ({ ...p, cached: p.raw !== undefined }))
    );
    setParameterWarning(undefined);
    setParameterError(undefined);
    try {
      const response = await api.request(
        name,
        'parameter-cache',
        {},
        { timeout: 10_000 }
      );
      const result = response.data as TParameters;
      setParameters(decodeParameters(result.parameters, true));
    } catch (error) {
      setParameterError(errorText(error));
    } finally {
      leave();
    }
  };
  const readParameter = async (parameter: TParameter) => {
    try {
      const response = await api.request(
        name,
        'parameters',
        { address: parameter.address },
        { timeout: 15_000 }
      );
      const readings = (response.data as TParameters).parameters.filter(
        (p) => p.address === parameter.address
      );
      const updated = decodeParameter(
        readings.length === 1 ? readings[0] : undefined
      );
      if (!updated || !hasRaw(updated))
        throw new Error('Could not verify the current parameter value.');
      setParameters((current) =>
        current?.map((p) => (p.address === updated.address ? updated : p))
      );
      return updated;
    } catch (error) {
      setParameters((current) =>
        current.map((p) =>
          p.address === parameter.address
            ? { ...p, cached: p.raw !== undefined, error: errorText(error) }
            : p
        )
      );
      throw error;
    }
  };
  const editParameter = async (parameter: TParameter) => {
    if (!canEdit(parameter) || !enter()) return;
    setSelected({ ...parameter, cached: true });
    setInput('');
    setEditError(undefined);
    try {
      const updated = await readParameter(parameter);
      setSelected(updated);
      setInput(parameterInput(updated));
    } catch (error) {
      setEditError(errorText(error));
    } finally {
      leave();
    }
  };
  const refreshParameters = async (rows: TParameter[]) => {
    if (!enter()) return;
    cancelRefresh.current = false;
    setParameterError(undefined);
    setParameterWarning(undefined);
    setProgress({ done: 0, total: rows.length });
    let failures = 0,
      done = 0,
      consecutive = 0;
    try {
      for (const row of rows) {
        if (cancelRefresh.current || consecutive >= 3) break;
        try {
          await readParameter(row);
          consecutive = 0;
        } catch {
          ++failures;
          ++consecutive;
        }
        setProgress({ done: ++done, total: rows.length });
      }
      if (failures || done < rows.length)
        setParameterWarning(
          `${done - failures} of ${rows.length} values refreshed.${cancelRefresh.current ? ' Cancelled.' : consecutive >= 3 ? ' Stopped after three consecutive failures.' : ''} Previous values retained.`
        );
    } finally {
      setProgress(undefined);
      leave();
    }
  };
  const encoded = encodeInput(selected, input);
  const saveParameter = async () => {
    if (
      !selected ||
      !editable(selected) ||
      encoded.raw === undefined ||
      encoded.raw === selected.raw ||
      !enter()
    )
      return;
    setEditError(undefined);
    let result: TWriteResult | undefined;
    let writeError: unknown;
    try {
      const response = await api.request(
        name,
        'parameter-set',
        {
          address: selected.address,
          raw: encoded.raw,
          expectedRaw: selected.raw,
        },
        { timeout: 15_000 }
      );
      result = response.data as TWriteResult;
    } catch (error) {
      writeError = error;
    }
    try {
      const updated = await readParameter(selected);
      setSelected(updated);
      if (result?.verified && updated.raw === encoded.raw)
        setSelected(undefined);
      else
        setEditError(
          writeError
            ? `${errorText(writeError)} Current value refreshed.`
            : result?.writeError
              ? `${result.writeError}. Current value refreshed.`
              : 'The requested value was not verified. Current value refreshed.'
        );
    } catch (error) {
      setEditError(
        `${writeError ? `${errorText(writeError)} ` : ''}${errorText(error)}`
      );
      const unavailable = {
        ...selected,
        raw: undefined,
        error: 'Read-back failed',
      };
      setSelected(unavailable);
      setParameters((current) =>
        current.map((p) =>
          p.address === selected.address
            ? { ...p, cached: p.raw !== undefined, error: 'Read-back failed' }
            : p
        )
      );
    } finally {
      leave();
    }
  };

  if (!state || (addr !== undefined && addr !== state.address)) return null;
  const statusKnown = fresh(state.status) && state.status.raw !== undefined;
  const running = statusKnown && !!(state.status.raw! & 1);
  const stopOnly = !statusKnown || running;
  const list: Array<[string, string | number]> = [
    [
      'Water pressure (set / current)',
      `${reading(state.pressureSetpoint, 1)} / ${reading(state.pressure, 1)} bar`,
    ],
    [
      'Output frequency (set / actual)',
      `${reading(state.frequencySetpoint, 2)} / ${reading(state.frequency, 2)} Hz`,
    ],
    ['Output voltage', `${reading(state.voltage, 0)} V`],
    ['Output current', `${reading(state.current, 1)} A`],
    [
      'Module temperature (estimate)',
      fresh(state.temperature)
        ? `Approx. ${reading(state.temperature, 1)} \u00b0C`
        : '--',
    ],
    ['Drive state', statusKnown ? (running ? 'RUN' : 'STOP') : 'Unknown'],
  ];

  return (
    <CardBox
      title={title}
      action={
        <Tooltip title="Modbus settings">
          <span>
            <IconButton
              aria-label={`Configure ${title}`}
              onClick={openSettings}
              disabled={busy}
            >
              <Settings />
            </IconButton>
          </span>
        </Tooltip>
      }
    >
      <DeviceDisabledAlert control={deviceControl} />
      {!statusKnown && (
        <Alert severity="warning">No recent drive status.</Alert>
      )}
      <NameValueList list={list} />
      {fresh(state.temperature) && state.temperature.raw !== undefined && (
        <Tooltip
          title={`d-13 raw ${rawText(state.temperature.raw)}; 0.1 \u00b0C per count is unverified.`}
        >
          <Typography variant="caption" component="p" sx={{ mt: 1 }}>
            Temperature raw: {state.temperature.raw}
          </Typography>
        </Tooltip>
      )}
      <Stack
        direction="row"
        spacing={1}
        useFlexGap
        sx={{ mt: 2, flexWrap: 'wrap' }}
      >
        <Button
          variant="contained"
          color={stopOnly ? 'error' : 'primary'}
          startIcon={stopOnly ? <Stop /> : <PlayArrow />}
          aria-pressed={running}
          disabled={busy || deviceControl.disabled || deviceControl.busy}
          onClick={() => {
            if (stopOnly)
              return run(false).catch((error) => snack.error(error));
            confirm('start');
          }}
        >
          {stopOnly ? 'Stop' : 'Start'}
        </Button>
        <Button
          variant="outlined"
          disabled={busy || deviceControl.disabled || deviceControl.busy}
          onClick={loadFaults}
        >
          Fault info
        </Button>
        <Button
          variant="outlined"
          disabled={busy || deviceControl.busy}
          onClick={loadParameters}
        >
          Configuration
        </Button>
      </Stack>
      <MessageBox {...messageBoxProps} />

      <Dialog
        open={settingsOpen}
        onClose={() => {
          if (!busy) setSettingsOpen(false);
        }}
        fullWidth
        maxWidth="xs"
        aria-labelledby={`${name}-settings-title`}
      >
        <DialogTitle id={`${name}-settings-title`}>Modbus settings</DialogTitle>
        <DialogContent>
          <DeviceDisableSetting control={deviceControl} busy={busy} />
          <Stack spacing={2} sx={{ mt: 1 }}>
            {busy && (
              <CircularProgress size={24} aria-label="Updating settings" />
            )}
            {settingsError && <Alert severity="error">{settingsError}</Alert>}
            <TextField
              label="Modbus slave address"
              value={address}
              onChange={(event) => setAddress(event.target.value)}
              disabled={
                busy || deviceControl.disabled || deviceControl.busy || !config
              }
              error={!addressValid}
              helperText="1-247"
              slotProps={{ htmlInput: { inputMode: 'numeric' } }}
              fullWidth
            />
            <TextField
              select
              label="Baud rate"
              value={baud}
              onChange={(event) => setBaud(Number(event.target.value))}
              disabled={
                busy || deviceControl.disabled || deviceControl.busy || !config
              }
              fullWidth
            >
              {BaudRates.map((value) => (
                <MenuItem key={value} value={value}>
                  {value}
                </MenuItem>
              ))}
            </TextField>
            <Typography variant="body2" color="text.secondary">
              Changing the baud rate also changes the shared Modbus bus speed.
            </Typography>
          </Stack>
        </DialogContent>
        <DialogActions>
          <Button disabled={busy} onClick={() => setSettingsOpen(false)}>
            Cancel
          </Button>
          <Button
            variant="contained"
            disabled={
              deviceControl.disabled ||
              deviceControl.busy ||
              busy ||
              !configValid ||
              !configChanged
            }
            onClick={saveSettings}
          >
            OK
          </Button>
        </DialogActions>
      </Dialog>

      <Dialog
        open={faultsOpen}
        onClose={() => {
          if (!busy) setFaultsOpen(false);
        }}
        fullWidth
        maxWidth="sm"
        aria-labelledby={`${name}-faults-title`}
      >
        <DialogTitle id={`${name}-faults-title`}>Fault info</DialogTitle>
        <DialogContent sx={{ px: { xs: 1, sm: 3 } }}>
          <Typography variant="subtitle2" sx={{ mb: 2 }}>
            Historical fault records
          </Typography>
          {busy && <CircularProgress size={24} aria-label="Reading faults" />}
          {faultError && <Alert severity="error">{faultError}</Alert>}
          {faults && dialogReadWarning(faults, faults.values) && (
            <Alert severity="warning">
              {dialogReadWarning(faults, faults.values)}
            </Alert>
          )}
          {faults && (
            <TableContainer>
              <Table
                size="small"
                aria-label="Fault history"
                sx={{ tableLayout: 'fixed' }}
              >
                <TableBody>
                  {FaultRegisters.map(([index, label]) => {
                    const register = faults.values.find(
                      (value) => value.address === 0x1000 + index
                    );
                    return (
                      <TableRow key={index}>
                        <TableCell
                          sx={{ width: '45%', px: 1, overflowWrap: 'anywhere' }}
                        >{`d-${index} ${label}`}</TableCell>
                        <TableCell sx={{ px: 1, overflowWrap: 'anywhere' }}>
                          <Tooltip
                            title={
                              register && hasRaw(register)
                                ? `Raw ${rawText(register.raw)}`
                                : registerError(register)
                            }
                          >
                            <span>{faultText(index, register)}</span>
                          </Tooltip>
                        </TableCell>
                      </TableRow>
                    );
                  })}
                </TableBody>
              </Table>
            </TableContainer>
          )}
        </DialogContent>
        <DialogActions>
          <Button disabled={busy} onClick={() => setFaultsOpen(false)}>
            Close
          </Button>
          <Button
            disabled={busy || deviceControl.disabled || deviceControl.busy}
            onClick={loadFaults}
          >
            Refresh
          </Button>
        </DialogActions>
      </Dialog>

      <Dialog
        open={parametersOpen}
        onClose={() => {
          if (!busy && !selected) setParametersOpen(false);
        }}
        fullWidth
        maxWidth="md"
        aria-labelledby={`${name}-parameters-title`}
        sx={{
          '& .MuiDialog-paper': {
            m: { xs: 1, sm: 4 },
            width: { xs: 'calc(100% - 16px)', sm: 'calc(100% - 64px)' },
            maxHeight: { xs: 'calc(100% - 16px)', sm: 'calc(100% - 64px)' },
          },
        }}
      >
        <DialogTitle id={`${name}-parameters-title`}>Configuration</DialogTitle>
        <DialogContent sx={{ px: { xs: 1, sm: 3 } }}>
          {busy && !selected && (
            <CircularProgress size={24} aria-label="Reading parameters" />
          )}
          {parameterError && <Alert severity="error">{parameterError}</Alert>}
          {parameterWarning && (
            <Alert severity="warning">{parameterWarning}</Alert>
          )}
          {progress && (
            <Typography variant="body2" role="status">
              {progress.done} / {progress.total}
            </Typography>
          )}
          {parameters && (
            <TableContainer>
              <Table
                size="small"
                aria-label="BFS parameters"
                sx={{ tableLayout: 'fixed' }}
              >
                <TableHead>
                  <TableRow>
                    <TableCell sx={{ px: 1 }}>Parameter</TableCell>
                    <TableCell sx={{ width: { xs: '40%', sm: '32%' }, px: 1 }}>
                      Value
                    </TableCell>
                    <TableCell sx={{ width: 72, px: 0 }} />
                  </TableRow>
                </TableHead>
                <TableBody>
                  {Groups.map(([group, label]) => [
                    <TableRow key={group}>
                      <TableCell
                        colSpan={3}
                        sx={{ bgcolor: 'action.hover', py: 1.5 }}
                      >
                        <Stack
                          direction="row"
                          sx={{
                            alignItems: 'center',
                            justifyContent: 'space-between',
                          }}
                        >
                          <Typography variant="subtitle2">
                            {group}: {label}
                          </Typography>
                          <Tooltip title={`Refresh ${group}`}>
                            <span>
                              <IconButton
                                size="small"
                                aria-label={`Refresh ${group}`}
                                disabled={
                                  busy ||
                                  deviceControl.disabled ||
                                  deviceControl.busy
                                }
                                onClick={() =>
                                  refreshParameters(
                                    parameters.filter((p) =>
                                      p.group.startsWith(`${group} `)
                                    )
                                  )
                                }
                              >
                                <Refresh fontSize="small" />
                              </IconButton>
                            </span>
                          </Tooltip>
                        </Stack>
                      </TableCell>
                    </TableRow>,
                    ...parameters
                      .filter((p) => p.group.startsWith(`${group} `))
                      .map((parameter) => (
                        <TableRow key={parameter.address}>
                          <TableCell sx={{ px: 1, overflowWrap: 'anywhere' }}>
                            <Typography variant="body2" component="div">
                              {parameter.id}
                            </Typography>
                            <Typography
                              variant="body2"
                              component="div"
                              color="text.secondary"
                            >
                              {parameter.name}
                            </Typography>
                          </TableCell>
                          <TableCell sx={{ px: 1, overflowWrap: 'anywhere' }}>
                            <Tooltip
                              title={
                                hasRaw(parameter)
                                  ? `Raw ${rawText(parameter.raw)}`
                                  : registerError(parameter)
                              }
                            >
                              <span>{parameterText(parameter)}</span>
                            </Tooltip>
                            {parameter.cached && (
                              <Typography
                                variant="caption"
                                component="div"
                                color="text.secondary"
                              >
                                Cached
                              </Typography>
                            )}
                            {parameter.error !== undefined && (
                              <Typography
                                variant="caption"
                                component="div"
                                color="error"
                              >
                                Refresh failed
                              </Typography>
                            )}
                          </TableCell>
                          <TableCell sx={{ px: 0 }}>
                            <Tooltip title={`Refresh ${parameter.id}`}>
                              <span>
                                <IconButton
                                  size="small"
                                  aria-label={`Refresh ${parameter.id}`}
                                  disabled={
                                    busy ||
                                    deviceControl.disabled ||
                                    deviceControl.busy
                                  }
                                  onClick={() => refreshParameters([parameter])}
                                >
                                  <Refresh fontSize="small" />
                                </IconButton>
                              </span>
                            </Tooltip>
                            <Tooltip
                              title={
                                canEdit(parameter)
                                  ? `Edit ${parameter.id}`
                                  : parameter.reason || 'Read-only'
                              }
                            >
                              <span>
                                <IconButton
                                  size="small"
                                  aria-label={`Edit ${parameter.id}`}
                                  disabled={
                                    busy ||
                                    deviceControl.disabled ||
                                    deviceControl.busy ||
                                    !canEdit(parameter)
                                  }
                                  onClick={() => editParameter(parameter)}
                                >
                                  <Edit fontSize="small" />
                                </IconButton>
                              </span>
                            </Tooltip>
                          </TableCell>
                        </TableRow>
                      )),
                  ])}
                </TableBody>
              </Table>
            </TableContainer>
          )}
        </DialogContent>
        <DialogActions>
          <Button
            disabled={busy || !!selected}
            onClick={() => setParametersOpen(false)}
          >
            Close
          </Button>
          {progress ? (
            <Button
              onClick={() => {
                cancelRefresh.current = true;
              }}
            >
              Cancel refresh
            </Button>
          ) : (
            <Button
              startIcon={<Refresh />}
              disabled={
                busy ||
                deviceControl.disabled ||
                deviceControl.busy ||
                !!selected
              }
              onClick={() => refreshParameters(parameters)}
            >
              Refresh all
            </Button>
          )}
        </DialogActions>
      </Dialog>

      <Dialog
        open={!!selected}
        onClose={() => {
          if (!busy) setSelected(undefined);
        }}
        fullWidth
        maxWidth="sm"
        aria-labelledby={`${name}-edit-title`}
      >
        <DialogTitle id={`${name}-edit-title`}>
          {selected?.id}: {selected?.name}
        </DialogTitle>
        <DialogContent>
          <Stack spacing={2} sx={{ mt: 1 }}>
            {busy && (
              <CircularProgress size={24} aria-label="Updating parameter" />
            )}
            {editError && <Alert severity="error">{editError}</Alert>}
            {selected && !editable(selected) && !busy && (
              <IconButton
                aria-label="Refresh current parameter"
                onClick={() => editParameter(selected)}
                disabled={deviceControl.disabled || deviceControl.busy}
                title="Refresh current parameter"
              >
                <Refresh />
              </IconButton>
            )}
            {selected && (
              <TextField
                autoFocus
                select={!!selected.options?.length}
                label={`Value${selected.unit ? ` (${selected.unit})` : ''}`}
                value={input}
                onChange={(event) => setInput(event.target.value)}
                disabled={
                  busy ||
                  deviceControl.disabled ||
                  deviceControl.busy ||
                  !editable(selected)
                }
                error={!!encoded.error}
                helperText={encoded.error}
                slotProps={{ htmlInput: { inputMode: 'decimal' } }}
                fullWidth
              >
                {selected.options?.map((option) => (
                  <MenuItem key={option.value} value={option.value}>
                    {option.label}
                  </MenuItem>
                ))}
              </TextField>
            )}
            {selected && hasRaw(selected) && (
              <Box>
                <Typography variant="body2" color="text.secondary">
                  Current: {parameterText(selected)}
                </Typography>
              </Box>
            )}
          </Stack>
        </DialogContent>
        <DialogActions>
          <Button disabled={busy} onClick={() => setSelected(undefined)}>
            Cancel
          </Button>
          <Button
            variant="contained"
            disabled={
              deviceControl.disabled ||
              deviceControl.busy ||
              busy ||
              encoded.raw === undefined ||
              encoded.raw === selected?.raw
            }
            onClick={saveParameter}
          >
            OK
          </Button>
        </DialogActions>
      </Dialog>
    </CardBox>
  );
};
