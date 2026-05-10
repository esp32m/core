import React, { useMemo, useState } from 'react';
import {
  Button,
  Checkbox,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  FormControl,
  FormControlLabel,
  Grid,
  IconButton,
  InputLabel,
  List,
  ListItem,
  MenuItem,
  Select,
  TextField,
  Tooltip,
  Typography,
} from '@mui/material';
import { Settings } from '@mui/icons-material';
import { CardBox } from '@ts-libs/ui-app';
import { useModuleState, useModuleApi } from '../../backend';
import {
  Name,
  TDebugOtState,
  TDebugOtConfig,
  RegDefs,
  RegFormat,
  DefaultPollIds,
  formatRegValue,
  parseRegValue,
  isSingleValueFormat,
} from './types';

/* ---- Register value table ---- */

const RegTable = ({ regs }: { regs: Record<string, number> }) => {
  const entries = useMemo(
    () =>
      Object.entries(regs)
        .map(([k, v]) => ({ id: parseInt(k, 10), raw: v }))
        .sort((a, b) => a.id - b.id),
    [regs]
  );
  if (!entries.length)
    return (
      <Typography variant="body2" color="text.secondary">
        No data yet — waiting for slave responses.
      </Typography>
    );
  return (
    <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: 13 }}>
      <thead>
        <tr>
          <th style={{ textAlign: 'left', width: 36 }}>ID</th>
          <th style={{ textAlign: 'left' }}>Name</th>
          <th style={{ textAlign: 'right' }}>Raw</th>
          <th style={{ textAlign: 'right', paddingLeft: 16 }}>Value</th>
        </tr>
      </thead>
      <tbody>
        {entries.map(({ id, raw }) => {
          const def = RegDefs[id];
          return (
            <tr key={id}>
              <td>{id}</td>
              <td>{def ? def.name : `ID-${id}`}</td>
              <td style={{ textAlign: 'right', fontFamily: 'monospace' }}>
                0x{raw.toString(16).padStart(4, '0')}
              </td>
              <td
                style={{
                  textAlign: 'right',
                  paddingLeft: 16,
                  fontFamily: 'monospace',
                }}
              >
                {def ? formatRegValue(raw, def.format) : String(raw)}
              </td>
            </tr>
          );
        })}
      </tbody>
    </table>
  );
};

/* ---- Poll-list settings dialog ---- */

const PollSettingsDialog = ({
  open,
  currentIds,
  onClose,
  onSave,
}: {
  open: boolean;
  currentIds: number[];
  onClose: () => void;
  onSave: (ids: number[]) => void;
}) => {
  const [selected, setSelected] = useState<Set<number>>(
    () => new Set(currentIds)
  );

  React.useEffect(() => {
    if (open) setSelected(new Set(currentIds));
  }, [open, currentIds]);

  const toggle = (id: number) => {
    setSelected((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  const allEntries = useMemo(
    () =>
      Object.entries(RegDefs)
        .map(([k, v]) => ({ id: parseInt(k, 10), ...v }))
        .sort((a, b) => a.id - b.id),
    []
  );

  return (
    <Dialog open={open} onClose={onClose} maxWidth="sm" fullWidth>
      <DialogTitle>Configure poll list</DialogTitle>
      <DialogContent dividers style={{ maxHeight: 400, overflowY: 'auto' }}>
        <List dense disablePadding>
          {allEntries.map(({ id, name }) => (
            <ListItem key={id} disablePadding>
              <FormControlLabel
                control={
                  <Checkbox
                    checked={selected.has(id)}
                    onChange={() => toggle(id)}
                    size="small"
                  />
                }
                label={`${id} — ${name}`}
              />
            </ListItem>
          ))}
        </List>
      </DialogContent>
      <DialogActions>
        <Button onClick={onClose}>Cancel</Button>
        <Button
          variant="contained"
          onClick={() => onSave(Array.from(selected).sort((a, b) => a - b))}
        >
          Save
        </Button>
      </DialogActions>
    </Dialog>
  );
};

/* ---- Write register form ---- */

const WriteForm = ({ name }: { name: string }) => {
  const api = useModuleApi(name);

  const writableEntries = useMemo(
    () =>
      Object.entries(RegDefs)
        .map(([k, v]) => ({ id: parseInt(k, 10), ...v }))
        .filter((e) => e.writable)
        .sort((a, b) => a.id - b.id),
    []
  );

  const formatOptions = [
    { label: 'f8.8 (fixed point)', value: RegFormat.F88 },
    { label: 'u16 (unsigned int)', value: RegFormat.U16 },
    { label: 's16 (signed int)', value: RegFormat.S16 },
  ];

  const [selectedId, setSelectedId] = useState<number>(
    writableEntries[0]?.id ?? 1
  );
  const [format, setFormat] = useState<RegFormat>(
    RegDefs[writableEntries[0]?.id]?.format ?? RegFormat.F88
  );
  const [valueText, setValueText] = useState('');
  const [busy, setBusy] = useState(false);

  const handleIdChange = (id: number) => {
    setSelectedId(id);
    const def = RegDefs[id];
    if (def && isSingleValueFormat(def.format)) setFormat(def.format);
    setValueText('');
  };

  const handleWrite = async () => {
    const raw = parseRegValue(valueText, format);
    if (raw === undefined)
      return;
    setBusy(true);
    try {
      await api.request('write', { id: selectedId, value: raw });
    } finally {
      setBusy(false);
    }
  };

  return (
    <Grid container spacing={2} alignItems="flex-end" sx={{ mt: 1 }}>
      <Grid size={{ xs: 12 }}>
        <Typography variant="subtitle2">Write register</Typography>
      </Grid>
      <Grid size={{ xs: 12, sm: 5 }}>
        <FormControl fullWidth size="small">
          <InputLabel>Register</InputLabel>
          <Select
            label="Register"
            value={selectedId}
            onChange={(e) => handleIdChange(e.target.value as number)}
          >
            {writableEntries.map(({ id, name: regName }) => (
              <MenuItem key={id} value={id}>
                {id} — {regName}
              </MenuItem>
            ))}
          </Select>
        </FormControl>
      </Grid>
      <Grid size={{ xs: 12, sm: 3 }}>
        <FormControl fullWidth size="small">
          <InputLabel>Format</InputLabel>
          <Select
            label="Format"
            value={format}
            onChange={(e) => setFormat(e.target.value as RegFormat)}
          >
            {formatOptions.map((o) => (
              <MenuItem key={o.value} value={o.value}>
                {o.label}
              </MenuItem>
            ))}
          </Select>
        </FormControl>
      </Grid>
      <Grid size={{ xs: 8, sm: 3 }}>
        <TextField
          fullWidth
          size="small"
          label="Value"
          value={valueText}
          onChange={(e) => setValueText(e.target.value)}
          onKeyDown={(e) => e.key === 'Enter' && handleWrite()}
        />
      </Grid>
      <Grid size={{ xs: 4, sm: 1 }}>
        <Button
          variant="outlined"
          size="small"
          disabled={busy || !valueText.trim()}
          onClick={handleWrite}
          fullWidth
        >
          Write
        </Button>
      </Grid>
    </Grid>
  );
};

/* ---- Main content component ---- */

export const content = ({ name = Name }: { name?: string } = {}) => {
  const state = useModuleState<TDebugOtState>(name);
  const api = useModuleApi(name);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [configIds, setConfigIds] = useState<number[]>(DefaultPollIds);

  React.useEffect(() => {
    api.getConfig<TDebugOtConfig>().then((cfg) => {
      if (cfg?.ids) setConfigIds(cfg.ids);
    });
  }, [api]);

  const handleSaveConfig = async (ids: number[]) => {
    setSettingsOpen(false);
    try {
      await api.setConfig<TDebugOtConfig>({ ids });
      setConfigIds(ids);
    } catch (e) {
      // error shown by setConfig itself
    }
  };

  return (
    <CardBox
      title="OpenTherm Master Debug"
      action={
        <Tooltip title="Configure poll list">
          <IconButton onClick={() => setSettingsOpen(true)} size="small">
            <Settings />
          </IconButton>
        </Tooltip>
      }
    >
      <RegTable regs={state?.regs ?? {}} />
      <WriteForm name={name} />
      <PollSettingsDialog
        open={settingsOpen}
        currentIds={configIds}
        onClose={() => setSettingsOpen(false)}
        onSave={handleSaveConfig}
      />
    </CardBox>
  );
};
