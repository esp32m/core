import { Name, TState, TProps, TConfig } from './types';
import { useBackendApi, useModuleConfig, useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';
import { Grid, IconButton } from '@mui/material';
import { Settings } from '@mui/icons-material';
import { DialogForm, FieldText, useDialogForm } from '@ts-libs/ui-forms';
import * as Yup from 'yup';

const validationSchema = Yup.object().shape({
  ctc: Yup.number().min(0).max(10),
  sc: Yup.number().min(0).max(1),
  tdsc: Yup.number().min(0).max(1),
  tcv: Yup.number(),
  mcc: Yup.number(),
  ccv: Yup.number().integer(),
});

export const Content = ({
  name = Name,
  title = 'Moisture sensor',
  addr,
}: TProps) => {
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
  const [age = -1, a, moisture, temperature, conductivity, salinity, tds] =
    state;
  if (age < 0 || age > 60 * 1000) return null;
  if (addr && addr != a) return null;
  const list = [];
  list.push(['Moisture', `${moisture?.toFixed(1)}`]);
  list.push(['Temperature', `${temperature?.toFixed(1)}`]);
  list.push(['Conductivity', `${conductivity}`]);
  list.push(['Salinity', `${salinity}`]);
  list.push(['TDS', `${tds}`]);
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
          <FieldText name="ctc" label="Conductivity temperature coeff" grid />
          <FieldText name="sc" label="Salinity coeff" grid />
          <FieldText name="tdsc" label="TDS coeff" grid />
          <FieldText name="tcv" label="Temperature calibration" grid />
          <FieldText name="mcc" label="Moisture calibration" grid />
          <FieldText name="ccv" label="Conductivity calibration" grid />
        </Grid>
      </DialogForm>
    </CardBox>
  );
};
