import { Name, TState, TProps, TConfig } from './types';
import { useModuleConfig, useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';
import { Grid, IconButton } from '@mui/material';
import { Settings } from '@mui/icons-material';
import { DialogForm, FieldText, useDialogForm } from '@ts-libs/ui-forms';
import * as Yup from 'yup';

const validationSchema = Yup.object().shape({
  pm25c: Yup.number(),
  pm10c: Yup.number(),
  pm1c: Yup.number(),
  tc: Yup.number(),
  hc: Yup.number(),
});

export const Content = ({ name = Name, title = 'Air Quality', addr }: TProps) => {
  const { config, setConfig } = useModuleConfig<TConfig>(name);
  const [hook, openSettings] = useDialogForm({
    initialValues: config || {},
    onSubmit: async (v) => setConfig(v),
    validationSchema,
  });
  const state = useModuleState<TState>(name);
  if (!state) return null;
  const [age = -1, a, pm25, pm10, pm1, temperature, humidity] = state;
  if (age < 0 || age > 60 * 1000) return null;
  if (addr && addr != a) return null;
  const list = [];
  list.push(['PM2.5', `${pm25} \u00b5g/m\u00b3`]);
  list.push(['PM10', `${pm10} \u00b5g/m\u00b3`]);
  list.push(['PM1.0', `${pm1} \u00b5g/m\u00b3`]);
  if (temperature !== undefined && !isNaN(temperature))
    list.push(['Temperature', `${temperature?.toFixed(1)} \u2103`]);
  if (humidity !== undefined && !isNaN(humidity))
    list.push(['Humidity', `${humidity?.toFixed(1)} %`]);
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
          <FieldText name="pm25c" label="PM2.5 calibration" grid />
          <FieldText name="pm10c" label="PM10 calibration" grid />
          <FieldText name="pm1c" label="PM1.0 calibration" grid />
          {config?.tc !== undefined && (
            <FieldText name="tc" label="Temperature calibration" grid />
          )}
          {config?.hc !== undefined && (
            <FieldText name="hc" label="Humidity calibration" grid />
          )}
        </Grid>
      </DialogForm>
    </CardBox>
  );
};
