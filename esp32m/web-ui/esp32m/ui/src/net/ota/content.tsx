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
  ToggleButton,
  ToggleButtonGroup,
  Typography,
} from '@mui/material';
import { useTranslation } from '@ts-libs/ui-i18n';
import * as Yup from 'yup';
import { OtaCheckName, OtaFeatures, TOtaCheckConfig, TOtaCheckState } from './types';
import { Expander, MessageBox, useMessageBox } from '@ts-libs/ui-base';
import { useRef, useState } from 'react';
import { useSnackApi } from '@ts-libs/ui-snack';
import { errorToString } from '@ts-libs/tools';
import { useFormikContext } from 'formik';

const validationSchema = Yup.object().shape({
  url: Yup.string().when('source', {
    is: 'url',
    then: (schema) => schema.required('URL must not be empty'),
    otherwise: (schema) => schema.optional(),
  }),
  file: Yup.mixed<File>().when('source', {
    is: 'file',
    then: (schema) => schema.required('Please select a firmware file'),
    otherwise: (schema) => schema.optional(),
  }),
});

const SourceToggle = () => {
  const { values, setFieldValue } = useFormikContext<any>();
  const { t } = useTranslation();
  return (
    <Grid size={{ xs: 12 }}>
      <ToggleButtonGroup
        value={values.source}
        exclusive
        onChange={(_, v) => v && setFieldValue('source', v)}
        size="small"
      >
        <ToggleButton value="url">{t('From URL')}</ToggleButton>
        <ToggleButton value="file">{t('From file')}</ToggleButton>
      </ToggleButtonGroup>
    </Grid>
  );
};

const UrlFields = () => {
  const { values } = useFormikContext<any>();
  if (values.source !== 'url') return null;
  return (
    <>
      <Grid size={{ xs: 12 }}>
        <FieldText name="url" label="Firmware URL" fullWidth />
      </Grid>
      <Grid size={{ xs: 12 }}>
        <FieldSwitch name="save" label="Remember this URL for future use" />
      </Grid>
    </>
  );
};

const FilePickerField = () => {
  const { values, setFieldValue, errors, touched } = useFormikContext<any>();
  const { t } = useTranslation();
  const inputRef = useRef<HTMLInputElement>(null);
  if (values.source !== 'file') return null;
  return (
    <Grid size={{ xs: 12 }}>
      <input
        ref={inputRef}
        type="file"
        accept=".bin"
        style={{ display: 'none' }}
        onChange={(e) => setFieldValue('file', e.target.files?.[0] ?? null)}
      />
      <Button variant="outlined" onClick={() => inputRef.current?.click()}>
        {values.file ? values.file.name : t('Select firmware file...')}
      </Button>
      {touched.file && errors.file && (
        <Typography variant="caption" color="error" sx={{ display: "block" }}>
          {errors.file as string}
        </Typography>
      )}
    </Grid>
  );
};

export const FirmwareUpdateButton = ({
  url,
  features = 0,
}: {
  url?: string;
  features?: number;
}) => {
  const { t } = useTranslation();
  const api = useBackendApi();
  const snackApi = useSnackApi();
  const showFileUpload = (features & OtaFeatures.FileUpload) !== 0;
  const [hook, open] = useDialogForm({
    initialValues: {
      source: showFileUpload ? 'file' : 'url',
      url: url || '',
      file: null,
      save: !!url,
      confirm: false,
    },
    onSubmit: async (values) => {
      const { source, url, file, confirm, save } = values;
      if (!confirm) throw new Error('Please read the disclaimer and agree');
      if (source === 'file') {
        if (!file) throw new Error('Please select a firmware file');
        // Start upload without awaiting — dialog closes immediately and the
        // OTA progress overlay takes over. Errors are reported via snack.
        fetch('/ota-upload', {
          method: 'POST',
          headers: { 'Content-Type': 'application/octet-stream' },
          body: file,
        })
          .then((resp) => {
            if (!resp.ok)
              snackApi.error(new Error(`Upload failed: ${resp.statusText}`));
          })
          .catch((e) => snackApi.error(e));
      } else {
        await api.request(Name, 'update', { url, save });
      }
    },
    validationSchema,
  });
  return (
    <>
      <Button onClick={() => open()}>{t('Update firmware')}</Button>
      <DialogForm title="Update firmware" hook={hook}>
        <Grid container spacing={2}>
          {showFileUpload && <SourceToggle />}
          <UrlFields />
          <FilePickerField />
          <Grid size={{ xs: 12 }}>
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
          </Grid>
          <Grid size={{ xs: 12 }}>
            <FieldSwitch name="confirm" label="I understand and agree" />
          </Grid>
        </Grid>
      </DialogForm>
    </>
  );
};

export const OtaCheck = () => {
  const api = useBackendApi();
  const state = useModuleState<TOtaCheckState>(OtaCheckName);
  const { config, refreshConfig } =
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
        <Grid size={{xs:12}}>
          {alertProps && <Alert {...alertProps} />}
        </Grid>
        <Grid size={{xs:12}}>
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
        <Grid size={{xs:12}}>
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
        <Grid size={{xs:12}} container sx={{ justifyContent: 'flex-end' }}>
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
                    message: `${t('Firmware update is available, version')} ${response.data
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
            <Grid size={{ xs: 'grow' }}>
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
