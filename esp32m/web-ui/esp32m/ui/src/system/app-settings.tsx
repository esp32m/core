import {
  Button,
  FormControlLabel,
  Grid,
  InputAdornment,
  Switch,
  styled,
} from '@mui/material';

import { ISystemConfig } from './types';
import * as Yup from 'yup';
import { useState } from 'react';
import { useBackendApi, useModuleConfig } from '../backend';
import { MessageBox, useMessageBox } from '@ts-libs/ui-base';
import { ConfigBox } from '../app';
import { FieldText } from '@ts-libs/ui-forms';
import { useFormikContext } from 'formik';
import { useTranslation } from '@ts-libs/ui-i18n';

const Name = 'app';

const ValidationSchema = Yup.object().shape({
  hostname: Yup.string()
    .min(4)
    .max(31)
    .matches(/^([a-z0-9](?:[a-z0-9-]*[a-z0-9]))$/),
});

const ResetButton = styled(Button)({
  bottom: 0,
  right: 0,
  position: 'absolute',
});

type TAppConfig = {
  hostname: string;
  udplog?: {
    enabled: boolean;
    host: string;
  };
};

const Inner = ({ updateConfig }: { updateConfig: VoidFunction }) => {
  const [requestInProgress, setRequestInProgress] = useState(false);
  const api = useBackendApi();
  const { t } = useTranslation();
  const {
    values: { udplog },
    setFieldValue,
    setFieldTouched,
  } = useFormikContext<TAppConfig>();
  const requestReset = async () => {
    setRequestInProgress(true);
    try {
      await api.request(Name, 'reset-hostname');
      updateConfig();
    } finally {
      setRequestInProgress(false);
    }
  };
  const enableUdplog = (enable: boolean) => {
    setFieldValue('udplog.enabled', enable);
    setFieldTouched('udplog.enabled');
    return true;
  };
  const { messageBoxProps, open } = useMessageBox({
    reset: {
      title: 'This will reset hostname to application default, proceed?',
      actions: [{ name: 'yes', onClick: requestReset }, 'no'],
    },
    udplog: {
      title:
        'This option will enable sending debug logs to the remote rsyslog server over UDP, proceed?',
      actions: [{ name: 'yes', onClick: () => enableUdplog(true) }, 'no'],
    },
  });

  return (
    <>
      <Grid container spacing={3}>
        <Grid item xs={12}>
          <FieldText
            name="hostname"
            label="Host name"
            fullWidth
            disabled={requestInProgress}
            InputProps={{
              endAdornment: (
                <InputAdornment position="end">
                  <ResetButton onClick={() => open('reset')}>
                    {t('Reset')}
                  </ResetButton>
                </InputAdornment>
              ),
            }}
          />
        </Grid>
        {udplog && (
          <Grid item xs={12}>
            <FormControlLabel
              control={
                <Switch
                  name="udplog.enabled"
                  checked={!!udplog?.enabled}
                  onChange={(e, checked) => {
                    if (checked) open('udplog');
                    else enableUdplog(false);
                  }}
                />
              }
              label={t('Enable remote logging')}
            />
            {udplog?.enabled && (
              <FieldText
                name="udplog.host"
                label="Host name or IP address of rsyslog server"
                fullWidth
              />
            )}
          </Grid>
        )}
      </Grid>
      <MessageBox {...messageBoxProps} />
    </>
  );
};

export const AppSettings = () => {
  const [config, updateConfig] = useModuleConfig<ISystemConfig>(Name);
  if (!config) return null;

  return (
    <ConfigBox
      name={'app'}
      initial={config}
      onChange={updateConfig}
      title="Application settings"
      validationSchema={ValidationSchema}
    >
      <Inner updateConfig={updateConfig} />
    </ConfigBox>
  );
};
