import { Name, TState, TProps, TConfig } from './types';
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
import { useMemo } from 'react';

const validationSchema = Yup.object().shape({
  decimals: Yup.number().integer().min(0).max(3),
  zr: Yup.number().integer(),
  fp: Yup.number().integer(),
});

const UnitNames = ['Unknown', 'cm', 'mm', 'MPa', 'Pa', 'KPa', 'Ma'];

export const Content = ({
  name = Name,
  title = 'Pressure sensor',
  addr,
}: TProps) => {
  const unitOptions = useMemo(
    () =>
      UnitNames.map((name, i) => (
        <MenuItem value={i} key={i}>
          {name}
        </MenuItem>
      )),
    []
  );

  const api = useBackendApi();
  const { config, setConfig } = useModuleConfig<TConfig>(name);
  const [hook, openSettings] = useDialogForm({
    initialValues: config || {},
    onSubmit: (v) => setConfig(v),
    validationSchema,
  });
  const state = useModuleState<TState>(name);
  if (!state) return null;
  const [age = -1, a, value] = state;
  if (age < 0 || age > 60 * 1000) return null;
  if (addr && addr != a) return null;
  const list = [];
  list.push(['Pressure', `${value} ${UnitNames[config?.unit || 0]}`]);
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
          <FieldSelect name="unit" label="Units" grid type="number">
            {unitOptions}
          </FieldSelect>
          <FieldText
            name="decimals"
            label="Decimal places"
            grid
            type="number"
          />
          {config && Object.hasOwn(config, 'zr') && (
            <FieldText name="zr" label="Zero range" grid type="number" />
          )}
          {config && Object.hasOwn(config, 'fp') && (
            <FieldText name="fp" label="Full point" grid type="number" />
          )}
        </Grid>
      </DialogForm>
    </CardBox>
  );
};
