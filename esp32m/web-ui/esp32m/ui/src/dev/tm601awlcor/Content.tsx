import { IProps, TState, TConfig } from './types';
import { useBackendApi, useModuleConfig, useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';
import { Grid, IconButton, MenuItem } from '@mui/material';
import { Settings } from '@mui/icons-material';
import {
  DialogForm,
  FieldSelect,
  FieldText,
  useDialogForm,
} from '@ts-libs/ui-forms';
import * as Yup from 'yup';

const validationSchema = Yup.object().shape({
  reg1: Yup.number().integer().min(0).max(255),
  reg2: Yup.number().integer().min(0).max(255),
  reg3: Yup.number().integer().min(0).max(255),
});


export default ({ name, title }: IProps) => {
  const state = useModuleState<TState>(name);
  const { config, setConfig } = useModuleConfig<TConfig>(name);
  const [hook, openSettings] = useDialogForm({
    initialValues: config || {},
    onSubmit: (v) => setConfig(v),
    validationSchema,
  });
  if (!state || !state.length) return null;
  const [age = -1, l] = state;
  if (age < 0 || age > 60 * 1000) return null;
  const list = [];
  if (!isNaN(l)) list.push(["Level", `${l?.toFixed(2)}`]);
  return (
    <CardBox title={title || name}
      action={
        <IconButton onClick={() => openSettings()}>
          <Settings />
        </IconButton>
      }
    >
      <NameValueList list={list} />
      <DialogForm title="Configure sensor" hook={hook}>
        <Grid container spacing={3}>
          {config && Object.hasOwn(config, 'reg1') && (
            <FieldText name="reg1" label="Register 1" grid type="number" />
          )}
          {config && Object.hasOwn(config, 'reg2') && (
            <FieldText name="reg2" label="Register 2" grid type="number" />
          )}
          {config && Object.hasOwn(config, 'reg3') && (
            <FieldText name="reg3" label="Register 3" grid type="number" />
          )}
        </Grid>
      </DialogForm>
    </CardBox>
  );
};
