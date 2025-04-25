import {
  DialogForm,
  FieldSwitch,
  FieldText,
  useDialogForm,
} from '@ts-libs/ui-forms';
import { useBackendApi, useModuleConfig } from '../backend';
import { Button, Grid } from '@mui/material';
import { useTranslation } from '@ts-libs/ui-i18n';
import * as Yup from 'yup';


type TAuthConfig = {
  enabled: boolean;
  username: string;
  password: string;
};
type TUiConfig = {
  auth: TAuthConfig;
};
const UiConfigName = 'ui';

export const UiAuthConfigButton = () => {
  const { config } = useModuleConfig<TUiConfig>(UiConfigName);
  const { t } = useTranslation();
  const api = useBackendApi();
  const validationSchema = Yup.object().shape({
    username: Yup.string().required(t('user name must not be empty')),
    password: Yup.string().required(t('password must not be empty')),
  });
  const [hook, open] = useDialogForm({
    initialValues: config?.auth || {},
    onSubmit: async (values) => {
      const { username, password } = values || {};
      if (!username || !password)
        throw new Error('Please provide username and password');
      await api.setConfig(UiConfigName, { auth: values });
    },
    validationSchema,
  });
  return (
    <>
      <Button onClick={() => open()}>{t('Configure UI authentication')}</Button>
      <DialogForm title="Configure UI authentication" hook={hook}>
        <Grid size={{ xs: 'grow' }}>
          <FieldSwitch name="enabled" label="Enable authentication" />
        </Grid>
        <Grid container spacing={3}>
          <Grid size={{ xs: 'grow' }}>
            <FieldText name="username" label="User name" fullWidth />
          </Grid>
          <Grid size={{ xs: 'grow' }}>
            <FieldText name="password" label="Password" fullWidth />
          </Grid>
        </Grid>
      </DialogForm>
    </>
  );
};
