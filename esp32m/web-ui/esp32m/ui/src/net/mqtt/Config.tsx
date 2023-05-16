import * as Yup from 'yup';
import { FormikConsumer } from 'formik';
import { Button, Grid, styled } from '@mui/material';

import { TMqttConfig, Name } from './types';
import { useState } from 'react';
import { useBackendApi, useModuleConfig } from '../../backend';
import { MessageBox, useMessageBox } from '@ts-libs/ui-base';
import { ConfigBox } from '../../app';
import { FieldPassword, FieldSwitch, FieldText } from '@ts-libs/ui-forms';
import { useTranslation } from '@ts-libs/ui-i18n';

const ValidationSchema = Yup.object().shape({
  keepalive: Yup.number().min(0),
  timeout: Yup.number().min(0),
});

const ButtonBar = styled(Grid)({ marginTop: 0 });

export const Config = () => {
  const [config, refresh] = useModuleConfig<TMqttConfig>(Name);
  const api = useBackendApi();
  const { t } = useTranslation();
  const [requestInProgress, setRequestInProgress] = useState(false);
  const requestCacheClear = async () => {
    setRequestInProgress(true);
    try {
      await api.request(Name, 'clear-cert-cache');
    } finally {
      setRequestInProgress(false);
    }
  };
  const { messageBoxProps, open } = useMessageBox({
    clearCache: {
      title:
        'This will remove cached SSL certificate and reload it from the specified URL on the next connection, proceed?',
      actions: [{ name: 'yes', onClick: requestCacheClear }, 'no'],
    },
  });
  if (!config) return null;

  return (
    <ConfigBox
      name={Name}
      initial={config}
      onChange={refresh}
      title="MQTT client settings"
      validationSchema={ValidationSchema}
    >
      <FormikConsumer>
        {(controller) => (
          <>
            <FieldSwitch name="enabled" label="Enable MQTT client" />
            {controller.values.enabled && (
              <>
                <Grid container spacing={3}>
                  <Grid item xs>
                    <FieldText name="uri" label="URL" fullWidth />
                  </Grid>
                  <Grid item xs>
                    <FieldText name="client" label="Client name" fullWidth />
                  </Grid>
                </Grid>
                <Grid container spacing={3}>
                  <Grid item xs>
                    <FieldText name="username" label="Username" fullWidth />
                  </Grid>
                  <Grid item xs>
                    <FieldPassword name="password" label="Password" fullWidth />
                  </Grid>
                </Grid>
                <Grid container spacing={3}>
                  <Grid item xs>
                    <FieldText
                      name="keepalive"
                      label="Keep alive period, s"
                      fullWidth
                    />
                  </Grid>
                  <Grid item xs>
                    <FieldText
                      name="timeout"
                      label="Network timeout, s"
                      fullWidth
                    />
                  </Grid>
                </Grid>
                <Grid container spacing={3}>
                  <Grid item xs>
                    <FieldText
                      name="cert_url"
                      label="SSL certificate URL"
                      fullWidth
                    />
                  </Grid>
                </Grid>
              </>
            )}
          </>
        )}
      </FormikConsumer>
      <ButtonBar container justifyContent="flex-end" spacing={2}>
        <Grid item>
          <Button
            onClick={() => open('clearCache')}
            disabled={requestInProgress}
          >
            {t('Clear local certificate cache')}
          </Button>
        </Grid>
      </ButtonBar>
      <MessageBox {...messageBoxProps} />
    </ConfigBox>
  );
};
