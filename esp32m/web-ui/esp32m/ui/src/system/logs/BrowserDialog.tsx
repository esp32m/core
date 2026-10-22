import { useEffect, useRef, useState } from 'react';
import {
  Alert,
  Box,
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  LinearProgress,
  MenuItem,
  Stack,
  TextField,
  Typography,
} from '@mui/material';
import { useTranslation } from '@ts-libs/ui-i18n';
import { errorToString } from '@ts-libs/tools';
import { useBackendApi } from '../../backend';
import { formatBytes } from '../../utils';
import {
  checkList,
  checkPage,
  downloadUrl,
  MaxCatalogFiles,
  pageBytes,
} from './api';
import { LogInfo, LogList, LogPage, Name } from './types';

export function BrowserDialog({
  info,
  onClose,
}: {
  info: LogInfo;
  onClose: VoidFunction;
}) {
  const api = useBackendApi();
  const { t } = useTranslation();
  const [catalog, setCatalog] = useState<LogList>();
  const [selected, setSelected] = useState('');
  const [page, setPage] = useState<LogPage>();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const active = useRef(true);
  const inFlight = useRef(false);
  const limit = pageBytes(info);
  const file = catalog?.files.find((entry) => entry.id === selected);

  async function perform(operation: () => Promise<void>) {
    if (!active.current || inFlight.current) return;
    inFlight.current = true;
    setBusy(true);
    setError('');
    try {
      await operation();
    } catch (e) {
      if (active.current) setError(errorToString(e) || 'Could not read logs');
    } finally {
      inFlight.current = false;
      if (active.current) setBusy(false);
    }
  }

  async function read(id: string, generation: string, offset?: number) {
    const response = await api.request(Name, 'read', {
      id,
      generation,
      limit,
      ...(offset === undefined ? { tail: true } : { offset }),
    });
    const result = checkPage(response.data, id);
    if (active.current) setPage(result);
  }

  const refresh = () =>
    perform(async () => {
      const response = await api.request(Name, 'list', { offset: 0 });
      let result = checkList(response.data);
      // Metadata pages are small and the complete catalog is hard bounded.
      // Read through them before choosing the newest file in an ascending list.
      let previousOffset = 0;
      while (result.nextOffset !== undefined && active.current) {
        if (
          result.nextOffset <= previousOffset ||
          result.files.length === 0 ||
          result.files.length >= MaxCatalogFiles
        )
          throw new Error('Log catalog exceeds the browser limit');
        previousOffset = result.nextOffset;
        const next = checkList(
          (await api.request(Name, 'list', { offset: previousOffset })).data
        );
        if (next.generation !== result.generation)
          throw new Error('Log catalog changed; refresh the file list');
        if (next.files.length === 0 && next.nextOffset !== undefined)
          throw new Error('Invalid log catalog cursor');
        const files = [...result.files, ...next.files];
        if (
          files.length > MaxCatalogFiles ||
          new Set(files.map((entry) => entry.id)).size !== files.length
        )
          throw new Error('Invalid log catalog');
        result = { ...next, files };
      }
      if (!active.current) return;
      setCatalog(result);
      const newest =
        result.files.find((entry) => entry.active) || result.files.at(-1);
      const chosen =
        result.files.find((entry) => entry.id === selected) || newest;
      setSelected(chosen?.id || '');
      setPage(undefined);
      if (chosen) await read(chosen.id, result.generation);
    });

  useEffect(() => {
    active.current = true;
    void refresh();
    return () => {
      active.current = false;
    };
    // Requests are manual after the dialog opens; no background history polling.
  }, []);

  const select = (id: string) =>
    perform(async () => {
      if (!catalog) return;
      setSelected(id);
      setPage(undefined);
      await read(id, catalog.generation);
    });

  return (
    <Dialog
      open
      onClose={onClose}
      fullWidth
      maxWidth="lg"
      aria-labelledby="logs-browser-title"
    >
      <DialogTitle id="logs-browser-title">{t('Stored logs')}</DialogTitle>
      {busy && <LinearProgress />}
      <DialogContent>
        <Stack spacing={2}>
          {error && (
            <Alert severity="error">
              {error}.{' '}
              {t('Refresh the file list if a log was rotated or removed.')}
            </Alert>
          )}
          <Stack
            direction="row"
            spacing={1}
            useFlexGap
            sx={{ flexWrap: 'wrap' }}
          >
            <Button disabled={busy} onClick={refresh}>
              {t('Refresh files')}
            </Button>
            <Button
              component="a"
              href={downloadUrl()}
              download
              target="_blank"
              rel="noopener"
              disabled={busy || !catalog?.files.length}
            >
              {t('Export all logs')}
            </Button>
          </Stack>
          {catalog && catalog.files.length === 0 && (
            <Alert severity="info">{t('No stored logs.')}</Alert>
          )}
          {!!catalog?.files.length && (
            <TextField
              select
              fullWidth
              label={t('Log file')}
              value={selected}
              disabled={busy}
              onChange={(event) => select(event.target.value)}
            >
              {catalog.files.map((entry) => (
                <MenuItem key={entry.id} value={entry.id}>
                  {entry.id} · {formatBytes(entry.size)}
                  {entry.active ? ` · ${t('Current')}` : ''}
                </MenuItem>
              ))}
            </TextField>
          )}
          {file && (
            <Typography variant="body2" sx={{ overflowWrap: 'anywhere' }}>
              {t('Boot session')}: {file.bootId} ·{' '}
              {file.firstTime == null || file.lastTime == null
                ? t('Time unknown')
                : `${new Date(file.firstTime).toLocaleString()} – ${new Date(file.lastTime).toLocaleString()}`}
            </Typography>
          )}
          {selected && (
            <Stack
              direction="row"
              spacing={1}
              useFlexGap
              sx={{ flexWrap: 'wrap' }}
            >
              <Button
                disabled={busy || !page || page.offset === 0}
                onClick={() =>
                  perform(() =>
                    read(
                      selected,
                      page!.generation,
                      Math.max(0, page!.offset - limit)
                    )
                  )
                }
              >
                {t('Older')}
              </Button>
              <Button
                disabled={busy || !page || page.eof}
                onClick={() =>
                  perform(() => read(selected, page!.generation, page!.end))
                }
              >
                {t('Newer')}
              </Button>
              <Button
                disabled={busy}
                onClick={() =>
                  perform(() => read(selected, catalog!.generation))
                }
              >
                {t('Latest')}
              </Button>
              <Button
                component="a"
                href={downloadUrl(selected)}
                download
                target="_blank"
                rel="noopener"
                disabled={busy}
              >
                {t('Export file')}
              </Button>
            </Stack>
          )}
          {page && (
            <>
              <Typography variant="caption">
                {t('Bytes')}: {page.offset}–{page.end} / {page.size}
              </Typography>
              <Box
                component="pre"
                tabIndex={0}
                aria-label={t('Log contents')}
                sx={{
                  m: 0,
                  p: 1.5,
                  maxHeight: '55vh',
                  minHeight: 120,
                  overflow: 'auto',
                  whiteSpace: 'pre-wrap',
                  overflowWrap: 'anywhere',
                  fontFamily: 'monospace',
                  fontSize: 12,
                  bgcolor: 'action.hover',
                }}
              >
                {page.text || t('This log file is empty.')}
              </Box>
            </>
          )}
        </Stack>
      </DialogContent>
      <DialogActions>
        <Button onClick={onClose}>{t('Close')}</Button>
      </DialogActions>
    </Dialog>
  );
}
