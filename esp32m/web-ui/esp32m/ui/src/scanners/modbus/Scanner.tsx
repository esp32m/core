import { useEffect, useRef, useState } from 'react';
import {
  Alert, Button, Checkbox, FormControlLabel, LinearProgress, MenuItem,
  Stack, Tab, Table, TableBody, TableCell, TableContainer, TableHead,
  TableRow, Tabs, TextField, Typography,
} from '@mui/material';
import { CardBox } from '@ts-libs/ui-app';
import { errorToString } from '@ts-libs/tools';
import { useBackendApi, useRequest, useSubscribedState } from '../../backend';
import { Config, Input, Name, ReadCommands, State, WriteCommands } from './types';
import { hex, outcome, requestFor } from './values';

export default function Scanner() {
  const api = useBackendApi();
  const [remote, refresh, live] = useSubscribedState<State>(Name);
  const [config] = useRequest<Config>(Name, 'config-get');
  const state = remote;
  const [tab, setTab] = useState(0);
  const [readCmd, setReadCmd] = useState(3);
  const [writeCmd, setWriteCmd] = useState(6);
  const [input, setInput] = useState<Input>({ from: '1', to: '3', addr: '1', regs: '0', regc: '1', data: '' });
  const [confirm, setConfirm] = useState(false);
  const [submitting, setSubmitting] = useState(false);
  const [cancelling, setCancelling] = useState(false);
  const [error, setError] = useState<string>();
  const [showFailures, setShowFailures] = useState(false);
  const active = useRef(false);
  const initialized = useRef(false);
  useEffect(() => {
    if (!config || initialized.current) return;
    initialized.current = true;
    setInput(v => ({ ...v, from: String(config.from), to: String(config.to),
      addr: String(config.addr), regs: String(config.regs), regc: String(config.regc) }));
    if (config.cmd >= 1 && config.cmd <= 4) setReadCmd(config.cmd);
    else if ([5, 6, 15, 16].includes(config.cmd)) setWriteCmd(config.cmd);
  }, [config]);
  const stale = !!remote && !live;
  const busy = submitting || !!state?.busy;
  const blocked = busy || !state?.configured || stale;
  const edit = (key: keyof Input, value: string) => {
    initialized.current = true; setConfirm(false);
    setInput(v => ({ ...v, [key]: value }));
  };
  const preset = (address: number) => {
    initialized.current = true;
    setTab(0); setReadCmd(3); setConfirm(false);
    setInput(v => ({ ...v, from: String(address), to: String(address), addr: String(address), regs: '3', regc: '1' }));
  };
  const request = requestFor(tab, input, tab === 2 ? writeCmd : readCmd, confirm);
  const submit = async () => {
    if (blocked || !request || active.current) return;
    active.current = true; setSubmitting(true); setError(undefined); setConfirm(false);
    try {
      await api.request(Name, tab === 0 ? 'scan' : tab === 1 ? 'request' : 'write',
        request, { timeout: 60_000 });
      await refresh();
    } catch (e) {
      setError(errorToString(e) + '. Check the latest operation below before retrying.');
      try { await refresh(); } catch { /* preserve the original error */ }
    } finally {
      active.current = false; setSubmitting(false);
    }
  };
  const cancel = async () => {
    if (cancelling) return;
    setCancelling(true);
    try { await api.request(Name, 'cancel'); await refresh(); }
    catch (e) { setError(errorToString(e)); }
    finally { setCancelling(false); }
  };
  const field = (key: keyof Input, label: string, helperText?: string) =>
    <TextField key={key} label={label} value={input[key]} disabled={blocked}
      onChange={event => edit(key, event.target.value)} helperText={helperText}
      slotProps={{ htmlInput: { inputMode: key === 'data' ? 'text' : 'numeric' } }} />;
  const rows = state?.results ?? [];
  const responders = rows.filter(row => row[1] === 0);
  const visible = showFailures ? rows : responders;
  const bus = state ?? config;
  return (
    <CardBox title="Modbus scanner">
      <Stack spacing={2}>
        <Typography>Shared bus: {bus?.configured
          ? ['UART ' + bus.uart, bus.mode?.toUpperCase(), bus.baud + ' baud',
              bus.parity === 0 ? 'no parity' : bus.parity === 2 ? 'even parity' : 'odd parity'].join(' · ')
          : 'Not configured'}</Typography>
        <Typography variant="body2" color="text.secondary">
          Uses the existing bus settings and sends each request once. Other drivers continue polling.
          Scans add traffic and expected timeouts to RS485 health statistics; reset the comparison afterward.
        </Typography>
        {stale && <Alert severity="warning">Scanner status is stale. Reconnect before starting another operation.</Alert>}
        <Stack direction="row" spacing={1} useFlexGap sx={{ flexWrap: 'wrap' }}>
          <Button disabled={blocked} onClick={() => preset(1)}>Flomarvel · address 1</Button>
          <Button disabled={blocked} onClick={() => preset(3)}>Flomarvel · address 3</Button>
        </Stack>
        <Typography variant="caption">
          Presets select a read-only FC03 probe of register 3 (actual angle ×100). Click Scan to send.
          A reply confirms this address responds, not which physical device replied.
        </Typography>
        <Tabs value={tab} onChange={(_, value) => { setTab(value); setConfirm(false); }}>
          <Tab label="Scan" disabled={busy} /><Tab label="Read" disabled={busy} /><Tab label="Write" disabled={busy} />
        </Tabs>
        <Stack direction="row" spacing={2} useFlexGap sx={{ flexWrap: 'wrap' }}>
          {tab === 0 ? <>{field('from', 'From address')}{field('to', 'To address', '1–247; equal addresses test a single device')}</>
            : field('addr', 'Slave address', '1–247; broadcasts are excluded')}
          <TextField select label="Function" value={tab === 2 ? writeCmd : readCmd} disabled={blocked}
            onChange={event => { initialized.current = true; setConfirm(false);
              (tab === 2 ? setWriteCmd : setReadCmd)(Number(event.target.value)); }}>
            {(tab === 2 ? WriteCommands : ReadCommands).map(([code, label]) =>
              <MenuItem key={code} value={code}>{label}</MenuItem>)}
          </TextField>
          {field('regs', 'Start register / coil', 'Zero-based address; decimal or 0x hex')}
          {tab !== 0 && field('regc', 'Quantity', '1–125; FC16 max 123; FC05/06 exactly 1')}
        </Stack>
        {tab === 2 && <>
          {field('data', 'Values', 'Comma or space separated; decimal or 0x hex. Coils: 0 or 1.')}
          <Alert severity="warning">
            Writes change the physical device. One write is sent, followed by an independent read-back.
            A missing acknowledgment is not proof that the write failed.
          </Alert>
          <FormControlLabel label="I confirm writing these values to this slave address."
            control={<Checkbox checked={confirm} disabled={blocked} onChange={(_, checked) => setConfirm(checked)} />} />
        </>}
        {!request && !busy && <Typography variant="body2" color="text.secondary">
          Enter a valid address range, function, register and quantity{tab === 2 ? ', matching values, and confirm the write' : ''}.
        </Typography>}
        <Stack direction="row" spacing={2}>
          <Button variant="contained" disabled={blocked || !request} onClick={submit}>
            {tab === 0 ? 'Scan' : tab === 1 ? 'Read values' : 'Write once and verify'}
          </Button>
          {state?.busy && state.operation === 'scan' && <Button disabled={cancelling || state.cancelRequested}
            onClick={cancel}>Stop scan</Button>}
        </Stack>
        {error && <Alert severity="error">{error}</Alert>}
        {state && state.job > 0 && <>
          <Typography>
            Operation {state.job}: {state.operation} · {state.scanned}/{state.total} · {state.progress}% · {(state.elapsedMs / 1000).toFixed(1)} s
            {state.busy ? state.cancelRequested ? ' · stopping after current request' : ' · running'
              : state.cancelled ? ' · cancelled' : state.error ? ' · failed' : ' · complete'}
          </Typography>
          {state.busy && <LinearProgress variant="determinate" value={state.progress} />}
          <Typography variant="caption">
            FC{String(state.cmd).padStart(2, '0')} · start {state.regs} ({hex(state.regs)}) ·
            {state.operation === 'scan' ? ' addresses ' + state.from + '–' + state.to
              : ' slave ' + state.addr + ', quantity ' + state.regc}
          </Typography>
          {!!state.error && <Alert severity="warning">{outcome(state.error)}</Alert>}
          {state.operation === 'scan' ? <>
            <Typography>{responders.length} successful replies; {rows.filter(row => row[1] === 263).length} timeouts;
              {' '}{rows.filter(row => row[1] !== 0 && row[1] !== 263).length} other errors.</Typography>
            {!state.busy && !responders.length && <Alert severity="info">
              No successful replies for this function/register. This does not prove that these addresses have no device.
            </Alert>}
            <FormControlLabel label="Show unsuccessful probes" control={<Checkbox checked={showFailures}
              onChange={(_, checked) => setShowFailures(checked)} />} />
            <TableContainer sx={{ maxHeight: 400 }}><Table size="small" aria-label="Modbus scan results">
              <TableHead><TableRow><TableCell>Address</TableCell><TableCell>Result</TableCell><TableCell>Value</TableCell></TableRow></TableHead>
              <TableBody>{visible.map(([address, code, value]) => <TableRow key={address}>
                <TableCell>{address} ({hex(address)})</TableCell><TableCell>{outcome(code)}</TableCell>
                <TableCell>{value === undefined ? '—' : value + ' (' + hex(value) + ')'}</TableCell>
              </TableRow>)}</TableBody>
            </Table></TableContainer>
          </> : <>
            {state.operation === 'write' && !state.busy && <Alert severity={state.verified ? 'success' : 'warning'}>
              {state.verified ? 'Requested values observed in read-back.' : 'Write outcome is unverified. Check the device before another write.'}
              {!!state.writeError && ' Write acknowledgment: ' + outcome(state.writeError) + '.'}
              {!!state.verifyError && ' Read-back: ' + outcome(state.verifyError) + '.'}
            </Alert>}
            <TableContainer sx={{ maxHeight: 400 }}><Table size="small" aria-label="Modbus values">
              <TableHead><TableRow><TableCell>Register / coil</TableCell><TableCell>Decimal</TableCell><TableCell>Hex</TableCell></TableRow></TableHead>
              <TableBody>{state.values?.map((value, i) => <TableRow key={i}>
                <TableCell>{state.regs + i}</TableCell><TableCell>{value}</TableCell><TableCell>{hex(value)}</TableCell>
              </TableRow>)}</TableBody>
            </Table></TableContainer>
          </>}
        </>}
      </Stack>
    </CardBox>
  );
}
