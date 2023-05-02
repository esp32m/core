import * as Yup from 'yup';
import {
  MenuItem,
  List,
  ListItem,
  ListItemText,
  IconButton,
} from '@mui/material';
import { Delete } from '@mui/icons-material';

import { TWifiConfig, Name, WifiPower, ApEntries, ApEntryFlags } from './types';
import { Alert, Expander, useAlert } from '@ts-libs/ui-base';
import { useBackendApi, useModuleConfig } from '../../backend';
import { ConfigBox } from '../../app';
import { FieldSelect } from '@ts-libs/ui-forms';

const PowerOptions = [
  [undefined, 'Default'],
  [WifiPower.dBm_20, '20 dBm'],
  [WifiPower.dBm_18, '18 dBm'],
  [WifiPower.dBm_16, '16 dBm'],
  [WifiPower.dBm_15, '15 dBm'],
  [WifiPower.dBm_14, '14 dBm'],
  [WifiPower.dBm_13, '13 dBm'],
  [WifiPower.dBm_11, '11 dBm'],
  [WifiPower.dBm_8, '8 dBm'],
  [WifiPower.dBm_7, '7 dBm'],
  [WifiPower.dBm_5, '5 dBm'],
  [WifiPower.dBm_2, '2 dBm'],
];

function SavedAps({
  aps,
  refreshConfig,
}: {
  aps: ApEntries;
  refreshConfig: () => void;
}) {
  const { alertProps, check } = useAlert();
  const api = useBackendApi();
  const requestDeleteAp = (id: number) =>
    api.request(Name, 'delete-ap', { id });

  return (
    <Expander title="Saved access points" defaultExpanded>
      <Alert {...alertProps} />
      <List disablePadding>
        {aps.map((r, i) => {
          const [id, ssid, , flags, failcount, bssid] = r;
          const persistent = flags & ApEntryFlags.Fallback;
          let text = ssid;
          if (failcount) text += ` (${failcount} failures)`;
          return (
            <ListItem key={i}>
              <ListItemText primary={text} secondary={bssid} />
              {!persistent && (
                <IconButton
                  aria-label="delete"
                  onClick={() => check(requestDeleteAp(id).then(refreshConfig))}
                >
                  <Delete />
                </IconButton>
              )}
            </ListItem>
          );
        })}
      </List>
    </Expander>
  );
}

const ValidationSchema = Yup.object().shape({});

export const SettingsBox = () => {
  const [config, refreshConfig] = useModuleConfig<TWifiConfig>(Name);
  if (!config) return null;

  return (
    <ConfigBox
      name={Name}
      initial={config}
      title="WiFi settings"
      validationSchema={ValidationSchema}
    >
      <FieldSelect fullWidth name="txp" label="TX power">
        {PowerOptions.map((o) => (
          <MenuItem key={o[0] || '_'} value={o[0]}>
            {o[1]}
          </MenuItem>
        ))}
      </FieldSelect>
      {config.aps?.length && (
        <SavedAps aps={config.aps} refreshConfig={refreshConfig} />
      )}
    </ConfigBox>
  );
};
