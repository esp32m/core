import {
  DialogForm,
  FieldSwitch,
  FieldText,
  useDialogForm,
} from '@ts-libs/ui-forms';
import {
  deserializeEsp32mError,
  useBackendApi,
  useModuleConfig,
  useModuleState,
} from '../../backend';
import { Name } from './state';
import {
  Alert,
  AlertProps,
  Button,
  FormControlLabel,
  Grid,
  Switch,
} from '@mui/material';
import { useTranslation } from '@ts-libs/ui-i18n';
import * as Yup from 'yup';
import { OtaCheckName, TOtaCheckConfig, TOtaCheckState } from './types';
import { Expander, MessageBox, useMessageBox } from '@ts-libs/ui-base';
import { useState } from 'react';
import { useSnackApi } from '@ts-libs/ui-snack';
import { errorToString } from '@ts-libs/tools';

const validationSchema = Yup.object().shape({
  url: Yup.string().required('URL must not be empty'),
});

export const FirmwareUpdateButton = ({ url }: { url?: string }) => {
  const { t } = useTranslation();
  const api = useBackendApi();
  const [hook, open] = useDialogForm({
    initialValues: { url, save: !!url },
    onSubmit: async (values) => {
      const { url, confirm, save } = values;
      if (!confirm) throw new Error('Please read the disclaimer and agree');
      await api.request(Name, 'update', { url, save });
    },
    validationSchema,
  });
  return (
    <>
      <Button onClick={() => open()}>{t('Update firmware from URL')}</Button>
      <DialogForm title="Update firmware from URL" hook={hook}>
        <Grid container spacing={3}>
          <Grid item xs>
            <FieldText name="url" label="Firmware URL" fullWidth />
          </Grid>
        </Grid>
        <Grid item xs>
          <FieldSwitch name="save" label="Remember this URL for future use" />
        </Grid>
        <Alert severity="warning">
          <strong>{t('OTA_DISCLAIMER_TITLE')}</strong>
          <ul>
            <li>{t('OTA_DISCLAIMER_I1')}</li>
            <li>{t('OTA_DISCLAIMER_I2')}</li>
            <li>{t('OTA_DISCLAIMER_I3')}</li>
            <li>{t('OTA_DISCLAIMER_I4')}</li>
            <li>{t('OTA_DISCLAIMER_I5')}</li>
          </ul>
        </Alert>
        <FieldSwitch name="confirm" label="I understand and agree" />
      </DialogForm>
    </>
  );
};

export const OtaCheck = () => {
  const api = useBackendApi();
  const state = useModuleState<TOtaCheckState>(OtaCheckName);
  const [config, refreshConfig] =
    useModuleConfig<TOtaCheckConfig>(OtaCheckName);
  const snackApi = useSnackApi();
  const [waitingResponse, setWaitingResponse] = useState<boolean>(false);
  const { t } = useTranslation();
  const { messageBoxProps, open } = useMessageBox({
    update: {
      title: 'Do you really want to perform firmware update ?',
      actions: [
        { name: 'yes', onClick: () => api.request(Name, 'update') },
        'no',
      ],
    },
  });
  const [configHook, openConfig] = useDialogForm({
    initialValues: { checkinterval: config?.checkinterval || 120 },
    onSubmit: async ({ checkinterval }) => {
      await api.setConfig(OtaCheckName, { checkinterval });
    },
    validationSchema: Yup.object().shape({
      checkinterval: Yup.number().integer().required(),
    }),
  });
  const hasNewVer = !!state?.newver;
  const alertProps: AlertProps | undefined = state?.lastcheck
    ? state.running
      ? {
          severity: 'info',
          children: <>{t('Checking for updates now...')}</>,
        }
      : hasNewVer
        ? {
            severity: 'info',
            action: (
              <Button
                color="inherit"
                size="small"
                onClick={() => open('update')}
              >
                {t('update now')}
              </Button>
            ),
            children: (
              <>{`${t(
                'Firmware update is available, version'
              )} ${state?.newver}`}</>
            ),
          }
        : state?.error
          ? {
              severity: 'error',
              children: (
                <>{errorToString(deserializeEsp32mError(state.error))}</>
              ),
            }
          : {
              severity: 'success',
              children: <>{t('Firmware is up to date')}</>,
            }
    : undefined;
  return (
    <Expander title="Firmware updates" defaultExpanded>
      <Grid container spacing={1}>
        <Grid item xs={12}>
          {alertProps && <Alert {...alertProps} />}
        </Grid>
        <Grid item xs={12}>
          <FormControlLabel
            control={
              <Switch
                checked={!!config?.autocheck}
                disabled={waitingResponse}
                onChange={async (_, autocheck) => {
                  try {
                    setWaitingResponse(true);
                    await api.setConfig(OtaCheckName, {
                      autocheck,
                    });
                    await refreshConfig();
                  } catch (e) {
                    snackApi.error(e);
                  } finally {
                    setWaitingResponse(false);
                  }
                }}
              />
            }
            label={t('Check automatically')}
          />
        </Grid>
        <Grid item xs={12}>
          <FormControlLabel
            control={
              <Switch
                checked={!!config?.autoupdate}
                disabled={waitingResponse}
                onChange={async (_, autoupdate) => {
                  try {
                    setWaitingResponse(true);
                    await api.setConfig(OtaCheckName, {
                      autoupdate,
                    });
                    await refreshConfig();
                  } catch (e) {
                    snackApi.error(e);
                  } finally {
                    setWaitingResponse(false);
                  }
                }}
              />
            }
            label={t('Install automatically')}
          />
        </Grid>
        <Grid item xs={12} container justifyContent={'flex-end'}>
          <Button onClick={() => openConfig()} disabled={waitingResponse}>
            {t('configure')}
          </Button>
          <Button
            onClick={async () => {
              try {
                setWaitingResponse(true);
                const response = await api.request(OtaCheckName, 'check');
                if (!response.data)
                  snackApi.add({
                    message: 'Firmware is up to date',
                    severity: 'success',
                  });
                else
                  snackApi.add({
                    message: `${t('Firmware update is available, version')} ${
                      response.data
                    }`,
                    severity: 'info',
                  });
                await refreshConfig();
              } catch (e) {
                snackApi.error(e);
              } finally {
                setWaitingResponse(false);
              }
            }}
            disabled={waitingResponse}
          >
            {t('check now')}
          </Button>
        </Grid>
        <MessageBox {...messageBoxProps} />
        <DialogForm title="Configure firmware updates" hook={configHook}>
          <Grid container spacing={3}>
            <Grid item xs>
              <FieldText
                name="checkinterval"
                label="Check interval, minutes"
                fullWidth
              />
            </Grid>
          </Grid>
        </DialogForm>
      </Grid>
    </Expander>
  );
};
