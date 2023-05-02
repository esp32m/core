import React, { useMemo } from "react";
import { cloneDeep } from "@ts-libs/tools";
import { FormikProps } from "formik";
import * as Yup from "yup";

import {
  Alert,
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  Grid,
} from "@mui/material";
import {
  PinConfig,
  PinMode,
  GpioMode,
  PullMode,
  AdcAtten,
  CwScale,
  CwPhase,
  Name,
  LedcMode,
  LedcIntr,
  PcntCountMode,
  TouchTieOpt,
} from "./types";
import { LedcModes, LedcTimers, toMenuItem } from "./tools";
import { FieldSelect, FieldText, MuiForm } from "@ts-libs/ui-forms";
import { useBackendApi } from "../../backend";

const PinModes = [
  [PinMode.Undefined, "Undefined"],
  [PinMode.Digital, "Digital"],
  [PinMode.Adc, "Analog-to-Digital (ADC)"],
  [PinMode.Dac, "Digital-to-Analog (DAC)"],
  [PinMode.CosineWave, "Cosine wave generator"],
  [PinMode.Ledc, "LED control"],
  [PinMode.SigmaDelta, "Sigma-delta generator"],
  [PinMode.PulseCounter, "Pulse counter"],
  [PinMode.Touch, "Touch sensor"],
];

const GpioModes: Array<[GpioMode, string]> = [
  [GpioMode.Disabled, "Disabled"],
  [GpioMode.Input, "Input only"],
  [GpioMode.Output, "Output only"],
  [GpioMode.InputOutput, "Input and output"],
  [GpioMode.OutputOd, "Output only with open-drain"],
  [GpioMode.InputOutputOd, "Input and output with open-drain"],
];

const PullModes = [
  [PullMode.Up, "Pull-up only"],
  [PullMode.Down, "Pull-down only"],
  [PullMode.Both, "Pull-up and pull-down"],
  [PullMode.Floating, "Floating pin"],
];

const AdcAttens = [
  [AdcAtten.Db0, "No attenuation (<800mv)"],
  [AdcAtten.Db2_5, "2.5dB (<1100mv)"],
  [AdcAtten.Db6, "6dB (<1350mv)"],
  [AdcAtten.Db11, "11dB (<2600mv)"],
];

const CwScales = [
  [CwScale.A1, "Maximum"],
  [CwScale.A1_2, "1/2"],
  [CwScale.A1_4, "1/4"],
  [CwScale.A1_8, "1/8"],
];

const CwPhases = [
  [CwPhase.P0, "No phase shift"],
  [CwPhase.P180, "180 degrees shift"],
];

const LedcIntrs = [
  [LedcIntr.Disable, "Disabled"],
  [LedcIntr.FadeEnd, "Enabled"],
];

const PcntCountModes = [
  [PcntCountMode.Disable, "Disabled"],
  [PcntCountMode.Increment, "Increment"],
  [PcntCountMode.Decrement, "Decrement"],
];

const TouchTieOpts = [
  [TouchTieOpt.Low, "Low"],
  [TouchTieOpt.High, "High"],
];

interface IPinConfig {
  mode: PinMode;
  digital?: {
    mode: GpioMode;
    pull: PullMode;
  };
  adc?: {
    atten: AdcAtten;
  };
  dac?: {
    voltage: number;
  };
  cw?: {
    offset: number;
    scale: CwScale;
    phase: CwPhase;
    freq: number;
  };
  ledc?: {
    channel: number;
    mode: LedcMode;
    intr: LedcIntr;
    timer: number;
    duty: number;
    hpoint: number;
  };
  sd?: {
    channel: number;
    duty: number;
    prescale: number;
  };
  pc?: {
    unit: number;
    channel: number;
    pmode: PcntCountMode;
    nmode: PcntCountMode;
    hlim: number;
    llim: number;
    filter: number;
  };
  touch?: {
    threshold: number;
    slope: number;
    tie: TouchTieOpt;
  };
}

const DefaultPinConfig: IPinConfig = {
  mode: PinMode.Undefined,
  digital: {
    mode: GpioMode.Disabled,
    pull: PullMode.Up,
  },
  adc: {
    atten: AdcAtten.Db0,
  },
  dac: {
    voltage: 0,
  },
  cw: {
    offset: 0,
    scale: CwScale.A1,
    phase: CwPhase.P0,
    freq: 130,
  },
  ledc: {
    channel: 0,
    mode: LedcMode.LowSpeed,
    intr: LedcIntr.Disable,
    timer: 0,
    duty: 0,
    hpoint: 0,
  },
  sd: {
    channel: 0,
    duty: 0,
    prescale: 0,
  },
  pc: {
    unit: 0,
    channel: 0,
    pmode: PcntCountMode.Disable,
    nmode: PcntCountMode.Disable,
    hlim: 0,
    llim: 0,
    filter: 0,
  },
  touch: {
    threshold: 0,
    slope: 0,
    tie: TouchTieOpt.Low,
  },
};

const isInputOnly = (pin: number) => pin > 33 && pin < 40;
const isDac2 = (pin: number) =>
  [0, 2, 4, 12, 13, 14, 15, 25, 26, 27].includes(pin);

function isValidMode(pin: number, mode: PinMode) {
  switch (mode) {
    case PinMode.Undefined:
    case PinMode.Digital:
    case PinMode.PulseCounter:
      return true;
    case PinMode.Adc:
      return isInputOnly(pin) || isDac2(pin);
    case PinMode.Dac:
    case PinMode.CosineWave:
      return pin == 25 || pin == 26;
    case PinMode.Ledc:
    case PinMode.SigmaDelta:
      return !isInputOnly(pin);
    case PinMode.Touch:
      return [4, 0, 2, 15, 13, 12, 14, 27, 33, 32].includes(pin);
    default:
      return false;
  }
}

function fromConciseConfig(config: PinConfig): IPinConfig {
  const [mode] = config || [];
  const result: IPinConfig = Object.assign({}, cloneDeep(DefaultPinConfig), {
    mode,
  });
  switch (mode) {
    case PinMode.Digital:
      result.digital = {
        mode: config[1] || GpioMode.Disabled,
        pull: config[2] || PullMode.Up,
      };
      break;
    case PinMode.Adc:
      result.adc = { atten: config[1] || AdcAtten.Db0 };
      break;
    case PinMode.Dac:
      result.dac = { voltage: config[1] || 0 };
      break;
    case PinMode.CosineWave:
      result.cw = {
        offset: config[1] || 0,
        scale: config[2] || CwScale.A1,
        phase: config[3] || CwPhase.P0,
        freq: config[4] || 0,
      };
      break;
    case PinMode.Ledc:
      result.ledc = {
        channel: config[1] || 0,
        mode: config[2] || LedcMode.LowSpeed,
        intr: config[3] || LedcIntr.Disable,
        timer: config[4] || 0,
        duty: config[5] || 0,
        hpoint: config[6] || 0,
      };
      break;
    case PinMode.SigmaDelta:
      result.sd = {
        channel: config[1] || 0,
        duty: config[2] || 0,
        prescale: config[3] || 0,
      };
      break;
    case PinMode.PulseCounter:
      result.pc = {
        unit: config[1] || 0,
        channel: config[2] || 0,
        pmode: config[3] || PcntCountMode.Disable,
        nmode: config[4] || PcntCountMode.Disable,
        hlim: config[5] || 0,
        llim: config[6] || 0,
        filter: config[7] || 0,
      };
      break;
    case PinMode.Touch:
      result.touch = {
        threshold: config[1] || 0,
        slope: config[2] || 0,
        tie: config[3] || TouchTieOpt.Low,
      };
      break;
    default:
      result.mode = PinMode.Undefined;
      break;
  }
  return result;
}

function toConciseConfig(config: IPinConfig) {
  const result: Array<number> = [config.mode];
  switch (config.mode) {
    case PinMode.Digital:
      result.push(
        config.digital?.mode || GpioMode.Disabled,
        config.digital?.pull || PullMode.Up
      );
      break;
    case PinMode.Adc:
      result.push(config.adc?.atten || AdcAtten.Db0);
      break;
    case PinMode.Dac:
      result.push(config.dac?.voltage || 0);
      break;
    case PinMode.CosineWave:
      result.push(
        config.cw?.offset || 0,
        config.cw?.scale || CwScale.A1,
        config.cw?.phase || CwPhase.P0,
        config.cw?.freq || 0
      );
      break;
    case PinMode.Ledc:
      result.push(
        config.ledc?.channel || 0,
        config.ledc?.mode || LedcMode.LowSpeed,
        config.ledc?.intr || LedcIntr.Disable,
        config.ledc?.timer || 0,
        config.ledc?.duty || 0,
        config.ledc?.hpoint || 0
      );
      break;
    case PinMode.SigmaDelta:
      result.push(
        config.sd?.channel || 0,
        config.sd?.duty || 0,
        config.sd?.prescale || 0
      );
      break;
    case PinMode.PulseCounter:
      result.push(
        config.pc?.unit || 0,
        config.pc?.channel || 0,
        config.pc?.pmode || PcntCountMode.Disable,
        config.pc?.nmode || PcntCountMode.Disable,
        config.pc?.hlim || 0,
        config.pc?.llim || 0,
        config.pc?.filter || 0
      );
      break;
    case PinMode.Touch:
      result.push(
        config.touch?.threshold || 0,
        config.touch?.slope || 0,
        config.touch?.tie || TouchTieOpt.Low
      );
      break;
  }
  return result;
}

interface ISubformProps {
  pin: number;
}

const DigitalForm = (props: ISubformProps) => {
  const { pin } = props;
  return (
    <>
      <Grid item xs={6}>
        <FieldSelect name="digital.mode" label="I/O mode" fullWidth>
          {GpioModes.filter(
            (m) => m[0] < GpioMode.Output || !isInputOnly(pin)
          ).map(toMenuItem)}
        </FieldSelect>
      </Grid>
      <Grid item xs={6}>
        <FieldSelect name="digital.pull" label="Pull resistors" fullWidth>
          {PullModes.filter(
            (m) => m[0] == PullMode.Floating || !isInputOnly(pin)
          ).map(toMenuItem)}
        </FieldSelect>
      </Grid>
    </>
  );
};

const AdcForm = (props: ISubformProps) => {
  const { pin } = props;
  return (
    <>
      {isDac2(pin) && (
        <Grid item xs={12}>
          <Alert severity="warning">
            This pin belongs to ADC2 that is used by the Wi-Fi driver
          </Alert>
        </Grid>
      )}
      <Grid item xs={12}>
        <FieldSelect name="adc.atten" label="Attenuation" fullWidth>
          {AdcAttens.map(toMenuItem)}
        </FieldSelect>
      </Grid>
    </>
  );
};

const DacForm = (props: ISubformProps) => {
  return (
    <>
      <Grid item xs={12}>
        <FieldText name="dac.voltage" label="Voltage" fullWidth />
      </Grid>
    </>
  );
};

const CwForm = (props: ISubformProps) => {
  return (
    <>
      <Grid item xs={6}>
        <FieldSelect name="cw.scale" label="Amplitude" fullWidth>
          {CwScales.map(toMenuItem)}
        </FieldSelect>
      </Grid>
      <Grid item xs={6}>
        <FieldSelect name="cw.phase" label="Phase shift" fullWidth>
          {CwPhases.map(toMenuItem)}
        </FieldSelect>
      </Grid>
      <Grid item xs={6}>
        <FieldText name="cw.offset" label="Offset" fullWidth />
      </Grid>
      <Grid item xs={6}>
        <FieldText name="cw.freq" label="Frequency" fullWidth />
      </Grid>
    </>
  );
};

const LedcForm = (props: ISubformProps) => {
  return (
    <>
      <Grid item xs={6}>
        <FieldSelect name="ledc.channel" label="LEDC channel" fullWidth>
          {Array.from(Array(8).keys())
            .map((v) => [v, "Channel #" + v])
            .map(toMenuItem)}
        </FieldSelect>
      </Grid>
      <Grid item xs={3}>
        <FieldSelect name="ledc.mode" label="LEDC mode" fullWidth>
          {LedcModes.map(toMenuItem)}
        </FieldSelect>
      </Grid>
      <Grid item xs={3}>
        <FieldSelect name="ledc.intr" label="Fade interrupt" fullWidth>
          {LedcIntrs.map(toMenuItem)}
        </FieldSelect>
      </Grid>
      <Grid item xs={6}>
        <FieldSelect name="ledc.timer" label="LEDC timer" fullWidth>
          {LedcTimers}
        </FieldSelect>
      </Grid>
      <Grid item xs={3}>
        <FieldText name="ledc.duty" label="Duty" fullWidth />
      </Grid>
      <Grid item xs={3}>
        <FieldText name="ledc.hpoint" label="Hpoint value" fullWidth />
      </Grid>
    </>
  );
};

const SdForm = (props: ISubformProps) => {
  return (
    <>
      <Grid item xs={6}>
        <FieldSelect name="sd.channel" label="Sigma-delta channel" fullWidth>
          {Array.from(Array(8).keys())
            .map((v) => [v, "Channel #" + v])
            .map(toMenuItem)}
        </FieldSelect>
      </Grid>
      <Grid item xs={3}>
        <FieldText name="sd.duty" label="Duty" fullWidth />
      </Grid>
      <Grid item xs={3}>
        <FieldText name="sd.prescale" label="Prescale" fullWidth />
      </Grid>
    </>
  );
};

const PcntForm = (props: ISubformProps) => {
  return (
    <>
      <Grid item xs={6}>
        <FieldSelect name="pc.unit" label="Pulse counter unit" fullWidth>
          {Array.from(Array(8).keys())
            .map((v) => [v, "Unit #" + v])
            .map(toMenuItem)}
        </FieldSelect>
      </Grid>
      <Grid item xs={6}>
        <FieldSelect name="pc.channel" label="Pulse counter channel" fullWidth>
          {Array.from(Array(2).keys())
            .map((v) => [v, "Channel #" + v])
            .map(toMenuItem)}
        </FieldSelect>
      </Grid>
      <Grid item xs={6}>
        <FieldSelect name="pc.pmode" label="Positive edge count mode" fullWidth>
          {PcntCountModes.map(toMenuItem)}
        </FieldSelect>
      </Grid>
      <Grid item xs={6}>
        <FieldSelect name="pc.nmode" label="Negative edge count mode" fullWidth>
          {PcntCountModes.map(toMenuItem)}
        </FieldSelect>
      </Grid>
      <Grid item xs={4}>
        <FieldText name="pc.hlim" label="Max. value" fullWidth />
      </Grid>
      <Grid item xs={4}>
        <FieldText name="pc.llim" label="Min. value" fullWidth />
      </Grid>
      <Grid item xs={4}>
        <FieldText name="pc.filter" label="Filter" fullWidth />
      </Grid>
    </>
  );
};

const TouchForm = (props: ISubformProps) => {
  return (
    <>
      <Grid item xs={4}>
        <FieldText name="touch.threshold" label="Threshold" fullWidth />
      </Grid>
      <Grid item xs={4}>
        <FieldText
          name="touch.slope"
          label="Charge/discharge speed"
          fullWidth
        />
      </Grid>
      <Grid item xs={4}>
        <FieldSelect name="touch.tie" label="Initial charge" fullWidth>
          {TouchTieOpts.map(toMenuItem)}
        </FieldSelect>
      </Grid>
    </>
  );
};

const Mode2Form: { [key: number]: React.ComponentType<ISubformProps> } = {
  [PinMode.Digital]: DigitalForm,
  [PinMode.Adc]: AdcForm,
  [PinMode.Dac]: DacForm,
  [PinMode.CosineWave]: CwForm,
  [PinMode.Ledc]: LedcForm,
  [PinMode.SigmaDelta]: SdForm,
  [PinMode.PulseCounter]: PcntForm,
  [PinMode.Touch]: TouchForm,
};

const ValidationSchema = Yup.object().shape({
  dac: Yup.object().shape({
    voltage: Yup.number().integer().min(0).max(255),
  }),
  cw: Yup.object().shape({
    offset: Yup.number().integer().min(-128).max(127),
    freq: Yup.number().integer().min(130).max(55000),
  }),
  ledc: Yup.object().shape({
    duty: Yup.number()
      .integer()
      .min(0)
      .max(1048576 - 1),
    hpoint: Yup.number().integer().min(0).max(0xfffff),
  }),
  sd: Yup.object().shape({
    duty: Yup.number().integer().min(-128).max(127),
    prescale: Yup.number().integer().min(0).max(255),
  }),
  pc: Yup.object().shape({
    hlim: Yup.number().integer().min(-32768).max(32767),
    llim: Yup.number().integer().min(-32768).max(32767),
    filter: Yup.number().integer().min(0).max(1023),
  }),
  touch: Yup.object().shape({
    threshold: Yup.number().integer().min(0).max(65535),
    slope: Yup.number().integer().min(0).max(7),
  }),
});

const PinConfigForm = (props: {
  pin: number;
  onClose: () => void;
  config: PinConfig;
}) => {
  const api=useBackendApi();
  const cfg = useMemo(() => fromConciseConfig(props.config), [props.config]);
  const { onClose, pin } = props;
  const open = pin >= 0;
  if (!open) return null;
  const handleClose = () => {
    onClose();
  };
  const handleSubmit = async (values: any) => {
    const data = { pins: { [pin]: toConciseConfig(values) } };
    await api.setConfig(Name, data);
    onClose();
  };
  return (
    <MuiForm
      initial={cfg}
      enableReinitialize={false}
      onSubmit={handleSubmit}
      validationSchema={ValidationSchema}
    >
      {(controller: FormikProps<any>) => {
        const SubForm = Mode2Form[controller.values.mode];
        return (
          <Dialog open={open} onClose={handleClose}>
            <DialogTitle> Pin IO{pin} settings </DialogTitle>
            <DialogContent style={{ width: 480 }} dividers>
              <Grid container spacing={3}>
                <Grid item xs={12}>
                  <FieldSelect name="mode" label="Pin mode" fullWidth>
                    {PinModes.filter((m) =>
                      isValidMode(pin, m[0] as PinMode)
                    ).map(toMenuItem)}
                  </FieldSelect>
                </Grid>
                {SubForm && <SubForm pin={pin} />}
              </Grid>
            </DialogContent>
            <DialogActions>
              <Button onClick={handleClose} color="secondary">
                Cancel
              </Button>
              <Button onClick={() => controller.submitForm()} color="primary">
                Save
              </Button>
            </DialogActions>
          </Dialog>
        );
      }}
    </MuiForm>
  );
};

export default PinConfigForm;
