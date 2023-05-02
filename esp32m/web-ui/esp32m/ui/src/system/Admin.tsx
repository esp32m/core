import React from 'react';

import * as Backend from '../backend';
import { Button, TextField, InputAdornment, Grid } from '@mui/material';
import { styled } from '@mui/material/styles';
import { MessageBox, useMessageBox } from '@ts-libs/ui-base';
import { CardBox } from '@ts-libs/ui-app';
import { useTranslation } from '@ts-libs/ui-i18n';

const Name = 'app';

const UpdateButton = styled(Button)({
  bottom: 0,
  right: 0,
  position: 'absolute',
});
const ButtonBar = styled(Grid)({ marginTop: 48 });

export const Admin = () => {
  const api = Backend.useBackendApi();
  const requestRestart = () =>
    api.request(Name, 'restart').then(() => window.location.reload());
  const requestReset = () =>
    api.request(Name, 'reset').then(() => window.location.reload());
  const requestUpdate = (url: string) => api.request('ota', 'update', { url });
  const { t } = useTranslation();
  const [fwurl, setFwurl] = React.useState<string>('');
  const { messageBoxProps, open } = useMessageBox({
    restart: {
      title: 'Do you really want to restart the CPU ?',
      actions: [{ name: 'yes', onClick: requestRestart }, 'no'],
    },
    reset: {
      title: 'Do you really want to reset settings to their defaults ?',
      actions: [{ name: 'yes', onClick: requestReset }, 'no'],
    },
    update: {
      title: 'Do you really want to perform firmware update ?',
      actions: [{ name: 'yes', onClick: () => requestUpdate(fwurl) }, 'no'],
    },
  });
  React.useEffect(() => {
    api.getConfig('ota').then((s) => {
      if (s.data?.url) setFwurl(s.data?.url);
    });
  }, []);
  return (
    <CardBox title="Administration">
      <Grid container>
        <Grid item xs>
          <TextField
            fullWidth
            variant="standard"
            label={t('Firmware URL')}
            value={fwurl}
            onChange={(e) => setFwurl(e.target.value)}
            InputProps={{
              endAdornment: (
                <InputAdornment position="end">
                  <UpdateButton
                    onClick={() => open('update')}
                    disabled={!fwurl}
                  >
                    {t('Update')}
                  </UpdateButton>
                </InputAdornment>
              ),
            }}
          />
        </Grid>
      </Grid>
      <ButtonBar container justifyContent="flex-end" spacing={4}>
        <Grid item>
          <Button onClick={() => open('restart')}>{t('system restart')}</Button>
        </Grid>
        <Grid item>
          <Button onClick={() => open('reset')}>{t('reset settings')}</Button>
        </Grid>
      </ButtonBar>
      <MessageBox {...messageBoxProps} />
    </CardBox>
  );
};
