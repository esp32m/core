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
  const { config, refreshConfig } = useModuleConfig<TMqttConfig>(Name);
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
      onChange={refreshConfig}
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
                  <FieldText name="uri" label="URL" fullWidth grid={{ size: { xs: 6 } }} />
                  <FieldText name="client" label="Client name" fullWidth grid={{ size: { xs: 6 } }} />
                </Grid>
                <Grid container spacing={3}>
                  <FieldText name="username" label="Username" fullWidth grid={{ size: { xs: 6 } }} />
                  <FieldPassword name="password" label="Password" fullWidth grid={{ size: { xs: 6 } }} />
                </Grid>
                <Grid container spacing={3}>
                  <FieldText
                    name="keepalive"
                    label="Keep alive period, s"
                    fullWidth
                    grid={{ size: { xs: 6 } }}
                  />
                  <FieldText
                    name="timeout"
                    label="Network timeout, s"
                    fullWidth grid={{ size: { xs: 6 } }}
                  />
                </Grid>
                <Grid container spacing={3}>
                  <FieldText
                    name="cert_url"
                    label="SSL certificate URL"
                    fullWidth
                    grid
                  />
                </Grid>
              </>
            )}
          </>
        )}
      </FormikConsumer>
      <ButtonBar container justifyContent="flex-end" spacing={2}>
        <Grid>
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
