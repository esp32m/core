import { Name, TState, TProps, TConfig } from './types';
import { useBackendApi, useModuleConfig, useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';
import { Grid, IconButton } from '@mui/material';
import { Settings } from '@mui/icons-material';
import { DialogForm, FieldText, useDialogForm } from '@ts-libs/ui-forms';
import * as Yup from 'yup';

const validationSchema = Yup.object().shape({
  hst: Yup.number().min(-30).max(70),
  het: Yup.number().min(0).max(70),
  delay: Yup.number().integer().min(1).max(60000),
  sens: Yup.number().integer().min(500).max(3500),
});

export const Content = ({ name = Name, title, addr }: TProps) => {
  const api = useBackendApi();
  const [config, refresh] = useModuleConfig<TConfig>(name);
  const [hook, openSettings] = useDialogForm({
    initialValues: config || {},
    onSubmit: async (v) => {
      try {
        await api.setConfig(name, v);
      } finally {
        refresh();
      }
    },
    validationSchema,
  });
  const state = useModuleState<TState>(name);
  if (!state) return null;
  const [age = -1, a, value] = state;
  if (age < 0 || age > 60 * 1000) return null;
  if (addr && addr != a) return null;
  const list = [];
  list.push(['Rain', `${value}`]);
  if (!list.length) return null;
  if (!addr && a) list.push(['MODBUS address', a]);
  return (
    <CardBox
      title={title || name}
      action={
        <IconButton onClick={() => openSettings()}>
          <Settings />
        </IconButton>
      }
    >
      <NameValueList list={list} />
      <DialogForm title="Configure sensor" hook={hook}>
        <Grid container spacing={3}>
          <FieldText name="hst" label="Heating start" grid />
          <FieldText name="het" label="Heting end" grid />
          <FieldText name="delay" label="Delay" grid />
          <FieldText name="sens" label="Sensitivity" grid />
        </Grid>
      </DialogForm>
    </CardBox>
  );
};
