import {
  Alert, Box, Button, LinearProgress, Stack, Table, TableBody, TableCell,
  TableContainer, TableHead, TableRow, Tooltip, Typography,
} from '@mui/material';
import { CardBox } from '@ts-libs/ui-app';
import { useModuleApi, useSubscribedState, useSampleAge } from '../../backend';
import { NameValueList } from '../../app';
import { Name, peerName, score, State, value } from './types';

const Health = ({ label, health }: { label: string; health: number | null }) => (
  <Box sx={{ flex: 1, minWidth: 160 }}>
    <Typography color="text.secondary" variant="body2">{label}</Typography>
    <Typography variant="h4">{score(health)}</Typography>
    {health !== null && <LinearProgress variant="determinate"
      value={Math.min(100, Math.max(0, health))}
      color={health === 100 ? 'success' : 'warning'} />}
  </Box>
);

export const content = () => {
  const [state, refresh, live] = useSubscribedState<State>(Name);
  const lastAge = useSampleAge(state?.lastAttemptAtMs, Name);
  const elapsed = useSampleAge(state?.comparisonStartedAtMs, Name);
  const api = useModuleApi(Name);
  if (!state) return null;
  const stale = !live;
  const recent = state.recent;
  const reset = async () => {
    if (await api.request('reset')) await refresh();
  };
  const list = [
    ['Attempts / replies / errors', `${recent.attempts} / ${recent.success} / ${recent.errors}`],
    ['Error rate', value(stale ? null : recent.errorRate, 2, '%')],
    ['Timeouts', `${recent.timeouts} (${value(recent.timeoutRate, 2, '%')})`],
    ['Invalid responses', `${recent.invalid} (${value(recent.invalidRate, 2, '%')})`],
    ['Other transport errors', `${recent.other} (${value(recent.otherRate, 2, '%')})`],
    ['Request rate', value(stale ? null : state.requestsPerSecond, 2, '/s')],
    ['Mean / maximum duration', `${value(recent.meanMs, 1, ' ms')} / ${value(recent.maxMs, 1, ' ms')}`],
    ['Mean successful response time', value(recent.successMs, 1, ' ms')],
    ['Last completed attempt', lastAge < 0 ? 'None' : value(lastAge / 1000, 1, ' s ago')],
    ['Comparison totals', `${state.total.attempts} attempts / ${state.total.errors} errors`],
  ];
  return (
    <CardBox title="RS485 bus health" action={
      <Tooltip title="Clear health statistics for a new comparison. Device settings are unaffected.">
        <span><Button onClick={reset} disabled={!!api.inProgress || stale}>Reset comparison</Button></span>
      </Tooltip>
    }>
      {stale && <Alert severity="warning">Health updates are stale. Reconnect before comparing changes.</Alert>}
      {!state.publisherRunning && <Alert severity="error">Sensor publisher is unavailable. InfluxDB metrics are not being updated.</Alert>}
      {!stale && !recent.attempts && <Alert severity="info">
        {state.windowMs < 1000 ? 'Collecting the first completed second.' : 'No bus traffic in the recent window. Health is unknown.'}
      </Alert>}
      <Stack direction="row" spacing={3} useFlexGap sx={{ mb: 2, mt: 1, flexWrap: 'wrap' }}>
        <Health label={`Recent health · ${state.windowSeconds} s`} health={stale ? null : recent.health} />
        <Health label="Cumulative health · since reset" health={stale ? null : state.total.health} />
      </Stack>
      <Typography variant="body2" color="text.secondary" sx={{ mb: 1 }}>
        Comparison {state.session} · {value(elapsed < 0 ? undefined : elapsed / 1000, 0)} s elapsed
        {state.windowMs < state.windowSeconds * 1000 && ` · Warming up (${value(state.windowMs / 1000, 1)} s collected)`}
      </Typography>
      <NameValueList list={list} />
      <TableContainer sx={{ mt: 2 }}>
        <Table size="small" aria-label="RS485 health by address">
          <TableHead><TableRow>
            <TableCell>Traffic</TableCell><TableCell align="right">Recent health</TableCell>
            <TableCell align="right">Errors / attempts</TableCell><TableCell align="right">Since reset</TableCell>
          </TableRow></TableHead>
          <TableBody>{state.peers.map((peer) => <TableRow key={`${peer.kind}-${peer.address}`}>
            <TableCell>{peerName(peer)}</TableCell>
            <TableCell align="right">{score(stale ? null : peer.recent.health)}</TableCell>
            <TableCell align="right">{peer.recent.errors} / {peer.recent.attempts}</TableCell>
            <TableCell align="right">{score(stale ? null : peer.total.health)}</TableCell>
          </TableRow>)}</TableBody>
        </Table>
      </TableContainer>
      {!!state.total.unverified && <Alert severity="info" sx={{ mt: 1 }}>
        {recent.unverified} recent / {state.total.unverified} total exchanges had no reply validation (raw traffic or broadcasts) and are excluded from health scores.
      </Alert>}
      <Typography variant="caption" color="text.secondary" component="p" sx={{ mt: 2 }}>
        Health = validated replies / checked attempts. Retries count separately.
        Recent metrics use the last 15 completed seconds (up to 1 s lag).
        Duration excludes intentional quiet intervals and UART queue wait.
        Invalid responses are transport errors, not a measured CRC-error count.
        Raw protocols include SM10; their timeout can mean no valid reply before the deadline.
        Passive monitoring adds no bus requests. Sensor metrics are published for InfluxDB even with this page closed.
      </Typography>
    </CardBox>
  );
};
