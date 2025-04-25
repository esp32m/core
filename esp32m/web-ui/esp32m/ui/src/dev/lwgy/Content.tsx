import { Name, TState, TProps, TConfig } from './types';
import { useBackendApi, useModuleConfig, useModuleState } from '../..';
import { NameValueList } from '../../app';
import { CardBox } from '@ts-libs/ui-app';
import { Button, Grid, IconButton, MenuItem } from '@mui/material';
import { Settings } from '@mui/icons-material';
import {
  DialogForm,
  FieldSelect,
  FieldText,
  useDialogForm,
} from '@ts-libs/ui-forms';
import * as Yup from 'yup';
import { useMemo, useState } from 'react';
import { useSnackApi } from '@ts-libs/ui-snack';

const validationSchema = Yup.object().shape({
  ctc: Yup.number().min(0).max(10),
  sc: Yup.number().min(0).max(1),
  tdsc: Yup.number().min(0).max(1),
  tcv: Yup.number(),
  mcc: Yup.number(),
  ccv: Yup.number().integer(),
});

const MoleculeUnitNames = ['Nm3', 'm3', 'L', 'USG', 'Kg', 'T', 'mL'];
const TimeUnitNames = ['h', 'min', 's'];

export const Content = ({
  name = Name,
  title = 'Flow transmitter',
  addr,
}: TProps) => {
  const moleculeUnitOptions = useMemo(
    () =>
      MoleculeUnitNames.map((name, i) => (
        <MenuItem value={i} key={i}>
          {name}
        </MenuItem>
      )),
    []
  );
  const timeUnitOptions = useMemo(
    () =>
      TimeUnitNames.map((name, i) => (
        <MenuItem value={i} key={i}>
          {name}
        </MenuItem>
      )),
    []
  );

  const api = useBackendApi();
  const snackApi = useSnackApi();
  const [resetting, setResetting] = useState(false);
  const resetTotals = async () => {
    try {
      setResetting(true);
      await api.request(name, 'reset');
    } catch (e) {
      snackApi.error(e);
    } finally {
      setResetting(false);
    }
  };

  const { config, setConfig } = useModuleConfig<TConfig>(name);
  const [hook, openSettings] = useDialogForm({
    initialValues: config || {},
    onSubmit: (v) => setConfig(v),
    validationSchema,
  });
  const state = useModuleState<TState>(name);
  if (!state) return null;
  const [
    age = -1,
    a,
    flow,
    consumption,
    id,
    decimals,
    density,
    flowunit,
    range,
    mc,
  ] = state;
  if (age < 0 || age > 60 * 1000) return null;
  if (addr && addr != a) return null;
  const list = [];
  const fu = config
    ? ` ${MoleculeUnitNames[config.um]}/${TimeUnitNames[config.ud]}`
    : '';
  const cu = config ? ` ${MoleculeUnitNames[config.um]}` : '';
  list.push(['Flow', `${flow?.toFixed(decimals)}${fu}`]);
  list.push(['Consumption', `${consumption?.toFixed(decimals)}${cu}`]);
  list.push(['Liquid density', `${density} kg/m3`]);
  if (id) list.push(['Unit id', `${id}`]);
  list.push(['Units', `${flowunit}`]);
  list.push(['Range', `${range}`]);
  list.push(['Meter coefficient', `${mc}`]);
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
      {config && (
        <DialogForm
          title="Configure sensor"
          hook={hook}
          buttons={[
            {
              name: 'reset',
              title: 'Reset totals',
              onClick: resetTotals,
              disabled: resetting,
            },
            { name: 'continue', submits: true },
            { name: 'cancel', cancels: true },
          ]}
        >
          <Grid container spacing={3}>
            <FieldSelect name="um" label="Molecule unit" grid fullWidth>
              {moleculeUnitOptions}
            </FieldSelect>
            <FieldSelect name="ud" label="Time unit" grid fullWidth>
              {timeUnitOptions}
            </FieldSelect>
            <FieldSelect name="ucf" label="Cumulative flow unit" grid fullWidth>
              {moleculeUnitOptions}
            </FieldSelect>
            <FieldSelect name="ums" label="Scale molecule unit" grid fullWidth>
              {moleculeUnitOptions}
            </FieldSelect>
          </Grid>
          <Grid container spacing={3}>
            <FieldSelect
              name="urd"
              label="Range denominator unit"
              grid
              fullWidth
            >
              {timeUnitOptions}
            </FieldSelect>
            <FieldText name="range" label="Range" grid />
            <FieldText name="sse" label="Small signal excision" grid />
            <FieldText name="dt" label="Damping time" grid />
          </Grid>
          <Grid container spacing={3}>
            <FieldText name="fd" label="Fluid density" grid />
            <FieldText name="sc" label="Sensor calibration" grid />
            <FieldText name="mc" label="Coefficient P/L" grid />
          </Grid>
        </DialogForm>
      )}
    </CardBox>
  );
};
