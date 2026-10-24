import { useEffect, useRef, useState } from 'react';
import { Alert, AlertTitle, Box, Button, Collapse, Typography } from '@mui/material';
import { TUiElementsPlugin } from '@ts-libs/ui-base';
import { NameValueList } from '../../app';
import { ConnectionStatus, useBackendApi, useSubscribedState } from '../../backend';
import { ResetReason } from '../../system/types';
import { Checkpoint, Name } from './types';

type TRecoveryImage = {
  slot: number;
  ver: string;
  sha256: string;
};

type TRecoveryIncident = {
  active: boolean;
  status: 'pending' | 'fallback' | 'testing' | 'resolved' | 'inactive';
  boot: number;
  reason: 'unexpected-reset';
  resetReason: number;
  checkpoint: number;
  uptime: number;
  strike: number;
  early: number;
  from: TRecoveryImage;
  to: TRecoveryImage;
};

type TRecoveryState = {
  supported: boolean;
  current: TRecoveryImage;
  recovery: TRecoveryIncident | null;
};

/** Probe each connection: an OTA restart may reconnect to older firmware. */
export function useCrashGuardRecovery() {
  const api = useBackendApi();
  const [supported, setSupported] = useState(false);
  const epoch = useRef(0);
  useEffect(() => {
    const update = () => {
      const current = ++epoch.current;
      setSupported(false);
      if (!api.status.is(ConnectionStatus.Connected)) return;
      void api.getInfo('app').then(
        ({ data }) => {
          if (current === epoch.current && api.status.is(ConnectionStatus.Connected))
            setSupported(data?.crashGuard === true);
        },
        () => {
          // Unsupported/older firmware has no global recovery subscription.
          if (current === epoch.current) setSupported(false);
        }
      );
    };
    const subscription = api.status.changed.subscribe(update);
    update();
    return () => {
      ++epoch.current;
      subscription.unsubscribe();
    };
  }, [api]);
  const watch = supported && api.status.is(ConnectionStatus.Connected);
  const [snapshot, , live] = useSubscribedState<TRecoveryState>(Name, {
    condition: watch,
    data: { view: 'recovery' },
  });
  // The shared state cache intentionally survives reconnects. Never display
  // its old incident until the current connection supplies a fresh snapshot.
  return watch && live && snapshot?.supported === true ? snapshot : undefined;
}

function imageLabel(image: TRecoveryImage) {
  const version = image.ver || 'Unknown version';
  return image.sha256 ? `${version} (${image.sha256.slice(0, 12)})` : version;
}

export function CrashGuardRecoveryNotice() {
  const state = useCrashGuardRecovery();
  return <CrashGuardRecoveryBanner state={state} />;
}

export function CrashGuardRecoveryBanner({ state }: { state?: TRecoveryState }) {
  const [details, setDetails] = useState(false);
  const recovery = state?.recovery;
  if (!state || !recovery?.active ||
      (recovery.status !== 'fallback' && recovery.status !== 'testing')) return null;
  const testing = recovery.status === 'testing';
  const list = [
    ['Recovery boot', recovery.boot],
    ['Affected firmware', imageLabel(recovery.from)],
    ['Selected backup firmware', imageLabel(recovery.to)],
    ['Running OTA partition', state.current.slot],
    ['Affected / backup OTA partitions', `${recovery.from.slot} / ${recovery.to.slot}`],
    ['Triggering reset', `${ResetReason[recovery.resetReason] || 'Unknown'} (${recovery.resetReason})`],
    ['Last checkpoint', `${Checkpoint[recovery.checkpoint] || 'Unknown'} (${recovery.checkpoint})`],
    ['Uptime before reset', `${(recovery.uptime / 1000).toFixed(3)} s`],
    ['Crash score / early crashes', `${recovery.strike} / ${recovery.early}`],
  ];
  return (
    <Alert severity="warning" sx={{ mb: 2, width: '100%', overflowWrap: 'anywhere' }}>
      <AlertTitle>
        {testing ? 'CrashGuard recovery: firmware is being evaluated' :
          'CrashGuard switched to backup firmware'}
      </AlertTitle>
      <Typography variant="body2">
        {testing ?
          'CrashGuard previously switched firmware after unexpected crashes or resets. The running firmware has not yet reached the stable checkpoint.' :
          'Unexpected crashes or resets affected the previously installed firmware.'}
      </Typography>
      <Typography variant="body2">Running: {imageLabel(state.current)}</Typography>
      <Typography variant="body2">Affected firmware: {imageLabel(recovery.from)}</Typography>
      <Button size="small" color="inherit" onClick={() => setDetails(!details)}
        aria-expanded={details} aria-controls="crashguard-recovery-details">
        {details ? 'Hide recovery details' : 'View recovery details'}
      </Button>
      <Collapse in={details} unmountOnExit>
        <Box id="crashguard-recovery-details">
          <NameValueList list={list} />
          <Typography variant="caption">
            The recorded counters describe the OTA partition at recovery time.
          </Typography>
        </Box>
      </Collapse>
    </Alert>
  );
}

export const CrashGuardRecoveryPlugin: TUiElementsPlugin = {
  name: 'crashguard-recovery-notice',
  ui: { content: { notices: [CrashGuardRecoveryNotice] } },
};
