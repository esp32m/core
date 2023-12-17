import { Button, Grid } from '@mui/material';
import { styled } from '@mui/material/styles';
import { MessageBox, useMessageBox } from '@ts-libs/ui-base';
import { CardBox } from '@ts-libs/ui-app';
import { useTranslation } from '@ts-libs/ui-i18n';
import { OtaFeatures, TOtaConfig } from '../net/ota/types';
import { useBackendApi, useModuleConfig } from '../backend';
import { FirmwareUpdateButton, OtaCheck } from '../net/ota';

const Name = 'app';

const ButtonBar = styled(Grid)({ marginTop: 24 });

export const Admin = () => {
  const api = useBackendApi();
  const requestRestart = () =>
    api.request(Name, 'restart').then(() => window.location.reload());
  const requestReset = () =>
    api.request(Name, 'reset').then(() => window.location.reload());
  const { t } = useTranslation();
  const [config] = useModuleConfig<TOtaConfig>('ota');
  const { messageBoxProps, open } = useMessageBox({
    restart: {
      title: 'Do you really want to restart the CPU ?',
      actions: [{ name: 'yes', onClick: requestRestart }, 'no'],
    },
    reset: {
      title: 'Do you really want to reset settings to their defaults ?',
      actions: [{ name: 'yes', onClick: requestReset }, 'no'],
    },
  });
  return (
    <CardBox title="Administration">
      {((config?.features || 0) & OtaFeatures.CheckForUpdates) != 0 && (
        <OtaCheck />
      )}
      <ButtonBar container spacing={2}>
        <Grid item>
          <Button onClick={() => open('restart')}>{t('system restart')}</Button>
        </Grid>
        <Grid item>
          <Button onClick={() => open('reset')}>{t('reset settings')}</Button>
        </Grid>
        {((config?.features || 0) & OtaFeatures.VendorOnly) == 0 && (
          <Grid item>
            <FirmwareUpdateButton url={config?.url} />
          </Grid>
        )}
      </ButtonBar>
      <MessageBox {...messageBoxProps} />
    </CardBox>
  );
};
