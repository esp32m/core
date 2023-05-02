import {
  Dialog,
  DialogTitle,
  DialogContent,
  Button,
  DialogActions,
  DialogProps,
} from '@mui/material';
import { FieldPassword, FieldText, MuiForm } from '@ts-libs/ui-forms';
import { useConnector } from './utils';
import { useTranslation } from '@ts-libs/ui-i18n';

export interface IProps {
  open: boolean;
  ssid: string;
  ssidEditable?: boolean;
  bssid: string;
  onClose: () => void;
}

function ConnectDialog(props: IProps) {
  const { ssid, bssid, open, onClose, ssidEditable } = props;
  const connector = useConnector();
  const { t } = useTranslation();

  const handleSubmit = async (values: any) => {
    await connector(values.ssid, values.bssid, values.password);
    onClose();
  };
  const initial = { ssid, bssid, password: '' };
  const dp: DialogProps = { open, onClose };
  return (
    <Dialog {...dp}>
      <DialogTitle>Connect to WiFi Access Point</DialogTitle>
      <DialogContent>
        <MuiForm
          initial={initial}
          onSubmit={handleSubmit}
          enableReinitialize={false}
        >
          {(controller) => {
            return (
              <>
                <FieldText
                  name="ssid"
                  label="SSID"
                  disabled={!ssidEditable}
                  fullWidth
                />
                <FieldText name="bssid" label="BSSID" fullWidth />
                <FieldPassword
                  name="password"
                  label="Password"
                  fullWidth
                  revealable
                />
                <DialogActions>
                  <Button onClick={() => onClose()} color="secondary">
                    {t('сancel')}
                  </Button>
                  <Button
                    onClick={() => controller.submitForm()}
                    color="primary"
                  >
                    {t('Connect')}
                  </Button>
                </DialogActions>
              </>
            );
          }}
        </MuiForm>
      </DialogContent>
    </Dialog>
  );
}

export default ConnectDialog;
