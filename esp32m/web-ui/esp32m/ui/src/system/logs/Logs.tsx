import { useEffect, useRef, useState } from 'react';
import { Alert, Button, Stack } from '@mui/material';
import { CardBox } from '@ts-libs/ui-app';
import { useTranslation } from '@ts-libs/ui-i18n';
import { NameValueList } from '../../app';
import { formatBytes } from '../../utils';
import { downloadUrl } from './api';
import { BrowserDialog } from './BrowserDialog';
import { SettingsDialog } from './SettingsDialog';
import { useLogs } from './useLogs';

export function Logs() {
  const { t } = useTranslation();
  const {
    connected,
    supported,
    info,
    state,
    live,
    config,
    loading,
    error,
    reloadMetadata,
  } = useLogs();
  const [browserOpen, setBrowserOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const available =
    connected && supported === true && live && state?.available === true;
  const availableRef = useRef(available);
  availableRef.current = available;
  const quotaPercent = state?.quotaPercent ?? config?.quotaPercent;
  const settingsInfo =
    info && state
      ? {
          ...info,
          freeBytes: state.freeBytes,
          reserveBytes: state.reserveBytes,
          quotaBaseBytes: state.quotaBaseBytes ?? info.quotaBaseBytes,
        }
      : info;

  useEffect(() => {
    if (!available) {
      setBrowserOpen(false);
      setSettingsOpen(false);
    }
  }, [available]);

  return (
    <CardBox title="Logs" progress={loading}>
      <Stack spacing={2}>
        {!connected && (
          <Alert severity="info">
            {t('Logs are unavailable while disconnected.')}
          </Alert>
        )}
        {connected && supported === false && (
          <Alert severity="info">
            {t('This firmware does not support stored logs.')}
          </Alert>
        )}
        {connected && supported && live && state?.available === false && (
          <Alert severity="warning">
            {t('Log storage is unavailable on this device.')}
          </Alert>
        )}
        {error && <Alert severity="error">{error}</Alert>}
        {state?.lastError && (
          <Alert severity="warning">{state.lastError}</Alert>
        )}
        {state && (
          <>
            <NameValueList
              list={[
                [
                  'Capture',
                  t(
                    state.enabled
                      ? state.ready
                        ? 'Recording'
                        : 'Paused'
                      : 'Disabled'
                  ),
                ],
                [
                  'Stored / quota',
                  `${formatBytes(state.usedBytes)} / ${formatBytes(state.quotaBytes)}`,
                ],
                ['Effective quota', formatBytes(state.effectiveQuotaBytes)],
                ...(quotaPercent
                  ? [['Quota policy', `${quotaPercent}${t('% of free space')}`]]
                  : []),
                ['Retained files', state.fileCount],
                [
                  'Filesystem free / reserve',
                  `${formatBytes(state.freeBytes)} / ${formatBytes(state.reserveBytes)}`,
                ],
                [
                  'Dropped / truncated records',
                  `${state.dropped} / ${state.truncated}`,
                ],
                ['Pending bytes', state.pendingBytes],
              ]}
            />
            {!state.timeValid && (
              <Alert severity="info">
                {t(
                  'Time is not synchronized. Logs with unknown dates expire by quota only.'
                )}
              </Alert>
            )}
            {(state.dropped > 0 || state.truncated > 0) && (
              <Alert severity="warning">
                {t(
                  'Some log records were dropped or truncated. Stored history is incomplete.'
                )}
              </Alert>
            )}
          </>
        )}
        <Stack direction="row" spacing={1} useFlexGap sx={{ flexWrap: 'wrap' }}>
          <Button
            disabled={!available || loading}
            onClick={() => setBrowserOpen(true)}
          >
            {t('Browse')}
          </Button>
          <Button
            component="a"
            href={downloadUrl()}
            download
            target="_blank"
            rel="noopener"
            disabled={!available || !state?.fileCount || loading}
          >
            {t('Export all logs')}
          </Button>
          <Button
            disabled={!available || !config || loading}
            onClick={async () => {
              if ((await reloadMetadata()) && availableRef.current)
                setSettingsOpen(true);
            }}
          >
            {t('Settings')}
          </Button>
        </Stack>
      </Stack>
      {browserOpen && available && info && (
        <BrowserDialog info={info} onClose={() => setBrowserOpen(false)} />
      )}
      {settingsOpen && available && settingsInfo && config && (
        <SettingsDialog
          info={settingsInfo}
          config={config}
          state={state}
          onClose={() => setSettingsOpen(false)}
          onSaved={() => {
            setSettingsOpen(false);
            void reloadMetadata();
          }}
        />
      )}
    </CardBox>
  );
}
