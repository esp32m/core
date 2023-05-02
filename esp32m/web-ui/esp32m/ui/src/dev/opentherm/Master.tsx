import { useMemo } from 'react';
import { IBounds, IMasterState, IProps, Status } from './types';
import * as Backend from '../../backend';
import { useModuleState } from '../..';
import {
  Grid,
  IconButton,
  FormControlLabel,
  Switch,
  SwitchProps,
} from '@mui/material';
import { Flame, Heater, TapWater } from './Icons';
import { Settings, Error } from '@mui/icons-material';
import { isFiniteNumber } from '../../utils';
import * as Yup from 'yup';
import { getIn, useFormikContext } from 'formik';
import { DialogForm, FieldText, useDialogForm } from '@ts-libs/ui-forms';
import { CardBox } from '@ts-libs/ui-app';
import { NameValueList } from '../../app';

const useValidationSchema = (bounds?: IBounds) => {
  const { dhw = [], ch = [], hcr = [] } = bounds || {};
  const hash = [...dhw, ...ch, ...hcr].reduce((pv, cv) => pv * 31 + cv, 17);
  return useMemo(() => {
    const tch = Yup.number()
      .min(ch[0] || 0)
      .max(ch[1] || 100);
    return Yup.object().shape({
      ts: Yup.number().test(
        (v) =>
          v == 0 ||
          (v !== undefined && v >= (ch[0] || 0) && v <= (ch[1] || 100))
      ),
      maxts: tch,
    });
  }, [hash]);
};

const FieldStatus = () => {
  const controller = useFormikContext<any>();
  const { values, setFieldValue, handleBlur: onBlur } = controller;
  const status = getIn(values, 'status') || 0;
  const setFlag = (flag: number, enable: boolean) => {
    let s = status;
    if (enable) s |= flag;
    else s &= ~flag;
    if (s != status) setFieldValue('status', s);
  };
  const pCh: SwitchProps = {
    checked: !!(status & Status.MasterCH),
    onChange: (e) => setFlag(Status.MasterCH, e.target.checked),
    onBlur,
  };
  const pTw: SwitchProps = {
    checked: !!(status & Status.MasterDHW),
    onChange: (e) => setFlag(Status.MasterDHW, e.target.checked),
    onBlur,
  };
  return (
    <Grid container spacing={3}>
      <Grid item xs>
        <FormControlLabel
          control={<Switch {...pCh} />}
          label="Central heating"
          labelPlacement="start"
        />
      </Grid>
      <Grid item xs>
        <FormControlLabel
          control={<Switch {...pTw} />}
          label="Tap water"
          labelPlacement="start"
        />
      </Grid>
    </Grid>
  );
};
export default ({ name, title }: IProps) => {
  const state = useModuleState<IMasterState>(name);
  const validationSchema = useValidationSchema(state?.hvac?.bounds);
  const api=Backend.useBackendApi();
  const [hook, openSettings] = useDialogForm({
    initialValues: state?.hvac||{},
    onSubmit: async (hvac) => {
      await api.setConfig(name, { hvac });
    },
    validationSchema,
  });
  if (!state) return null;
  const { status, ts, maxts, tdhw, tdhws, rm, chp, tb, bfc } = state.hvac;
  const slaveStatusIcons = [];
  if (status & Status.SlaveFault) slaveStatusIcons.push(<Error />);
  if (status & Status.SlaveFlame) slaveStatusIcons.push(<Flame />);
  if (status & Status.SlaveCH) slaveStatusIcons.push(<Heater />);
  if (status & Status.SlaveDHW) slaveStatusIcons.push(<TapWater />);
  const masterStatusIcons = [];
  if (status & Status.MasterCH) masterStatusIcons.push(<Heater />);
  if (status & Status.MasterDHW) masterStatusIcons.push(<TapWater />);

  const list = [];
  list.push([
    'Master/Slave status',
    <>
      {masterStatusIcons} / {slaveStatusIcons}
    </>,
  ]);
  if (isFiniteNumber(ts) || isFiniteNumber(maxts))
    list.push(['TSet / MaxTSet', `${ts?.toFixed(2)} / ${maxts?.toFixed(2)}`]);
  if (isFiniteNumber(tdhw) || isFiniteNumber(tdhws))
    list.push(['TDhw / TDhwSet', `${tdhw?.toFixed(2)} / ${tdhws?.toFixed(2)}`]);
  if (isFiniteNumber(rm)) list.push(['Modulation', `${rm?.toFixed(0)}`]);
  if (isFiniteNumber(chp))
    list.push(['CH water pressure', `${chp?.toFixed(2)} atm`]);
  if (isFiniteNumber(tb))
    list.push(['Boiler temperature', `${tb?.toFixed(2)}`]);
  if (isFiniteNumber(bfc))
    list.push(['Burner flame current', `${bfc?.toFixed(2)}`]);

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
      <DialogForm title="Configure HVAC" hook={hook}>
        <Grid container spacing={3}>
          <FieldStatus />
          <FieldText name="ts" label="CH temperature" grid />
          <FieldText name="maxts" label="Max CH temperature" grid />
          <FieldText name="tdhws" label="DHW Temperature" grid />
        </Grid>
      </DialogForm>
    </CardBox>
  );
};
