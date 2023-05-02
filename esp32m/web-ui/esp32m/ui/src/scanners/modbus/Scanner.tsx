import { useState } from 'react';
import { useSelector } from 'react-redux';
import * as Yup from 'yup';
import { useFormikContext, FormikConsumer } from 'formik';
import {
  Button,
  Grid,
  MenuItem,
  Typography,
  Divider,
  Tabs,
  Tab,
  Box,
  Paper,
} from '@mui/material';


import { Name } from './types';
import { styled } from '@mui/material/styles';
import { useBackendApi, useModuleConfig } from '../../backend';
import { validators } from '../../validation';
import { Alert, useAlert } from '@ts-libs/ui-base';
import { FieldAutocomplete, FieldSelect, FieldText, MuiForm } from '@ts-libs/ui-forms';
import { CardBox } from '@ts-libs/ui-app';

interface IOptions {
  from?: number;
  to?: number;
}

interface IScanResponse {
  progress?: number;
  addrs?: Array<number>;
}

const Results = styled('div')({
  marginTop: 16,
  padding: 16,
  paddingTop: 8,
  borderStyle: 'solid',
  borderWidth: 1,
  borderColor: 'rgba(0, 0, 0, 0.12)',
});

const ResultMsg = styled(Typography)({
  textAlign: 'center',
  width: '100%',
});
const IdsBox = styled(Grid)({
  marginTop: 10,
});
const ScanDiv = styled('div')({
  position: 'relative',
  width: '100%',
  height: '100%',
});
const StyledScanButton = styled(Button)({
  position: 'absolute',
  bottom: 0,
  right: 0,
});
const IdButton = styled(Button)({
  textTransform: 'none',
});
interface IModbusResponse {
  scan?: IScanResponse;
  request?: any;
}
interface IProps {
  state: IOptions;
  resp: IModbusResponse;
}

const SubmitButton = ({ label }: { label: string }) => {
  const { isSubmitting, submitForm } = useFormikContext();
  return (
    <ScanDiv>
      <StyledScanButton
        variant="contained"
        disabled={isSubmitting}
        onClick={submitForm}
      >
        {label}
      </StyledScanButton>
    </ScanDiv>
  );
};

const ScanResults = ({ state, resp }: IProps) => {
  const { from = 1, to = 247 } = state;
  const { addrs = [] } = resp?.scan || {};
  const buttons = [];
  for (let i = from; i <= to; i++)
    if (addrs[i >> 3] & (1 << (i & 7)))
      buttons.push(
        <Grid item xs={2} key={i}>
          <IdButton variant="contained" color="secondary">
            0x{i.toString(16)}
          </IdButton>
        </Grid>
      );
  const content = buttons.length ? (
    <IdsBox container spacing={3} wrap="wrap">
      {buttons}
    </IdsBox>
  ) : (
    <ResultMsg variant={'subtitle1'}>{'No devices were detected!'}</ResultMsg>
  );
  return (
    <Results>
      <Typography variant="subtitle1">Scan results</Typography>
      <Divider />
      {content}
    </Results>
  );
};

function TabPanel(props: any) {
  const { children, value, index, ...other } = props;

  return (
    <div
      role="tabpanel"
      hidden={value !== index}
      id={`full-width-tabpanel-${index}`}
      aria-labelledby={`full-width-tab-${index}`}
      {...other}
    >
      {value === index && <Box p={3}>{children}</Box>}
    </div>
  );
}

const ValidationSchema = Yup.object().shape({
  from: validators.modbusAddr,
  to: validators.modbusAddr,
  addr: validators.modbusAddr,
});

export default () => {
  const [tab, setTab] = useState(0);
  const { check, alertProps } = useAlert();
  const [state = {}, refreshConfig] = useModuleConfig<IOptions>(Name);
  const api=useBackendApi();
  const reqScan = (req?: IOptions) => api.request(Name, 'scan', req);
  const reqRequest = (req?: IOptions) => api.request(Name, 'request', req);

    const handleSubmit = async (values: any) => {
    switch (tab) {
      case 0:
        check(await reqScan(values));
        break;
      case 1:
        await check(reqRequest(values));
        break;
    }
    refreshConfig();
  };

  const ncp = {
    type: 'number',
    placeholder: 'auto',
    InputLabelProps: { shrink: true },
    fullWidth: true,
  };
  const resp = useSelector<any, IModbusResponse>((state) => state.modbus) || {};
  const { scan } = resp;
  return (
    <MuiForm
      initial={state}
      onSubmit={handleSubmit}
      validationSchema={ValidationSchema}
    >
      <FormikConsumer>
        {(controller) => {
          return (
            <CardBox
              title="MODBUS tools"
              progress={scan?.progress || controller.isSubmitting || false}
            >
              <Grid container spacing={3}>
                <Grid item xs>
                  <FieldSelect name="mode" label="RS485 mode" {...ncp}>
                    <MenuItem value="rtu">RTU</MenuItem>
                    <MenuItem value="ascii">ASCII</MenuItem>
                  </FieldSelect>
                </Grid>
                <Grid item xs>
                  <FieldSelect name="uart" label="UART number" {...ncp}>
                    <MenuItem value={0}>UART 0</MenuItem>
                    <MenuItem value={1}>UART 1</MenuItem>
                    <MenuItem value={2}>UART 2</MenuItem>
                  </FieldSelect>
                </Grid>
                <Grid item xs>
                  <FieldAutocomplete
                    name="baud"
                    label="Baud rate"
                    {...ncp}
                    autocompleteProps={{
                      freeSolo: true,
                      options: [
                        2400, 4800, 9600, 14400, 19200, 38400, 57600, 115200,
                      ],
                      getOptionLabel: (o: any) => o.toString(),
                    }}
                  />
                </Grid>
                <Grid item xs>
                  <FieldSelect name="parity" label="Parity" {...ncp}>
                    <MenuItem value={0}>Disable</MenuItem>
                    <MenuItem value={2}>Even</MenuItem>
                    <MenuItem value={3}>Odd</MenuItem>
                  </FieldSelect>
                </Grid>
              </Grid>
              <div style={{ marginTop: 20 }} />
              <Paper square>
                <Tabs
                  variant="fullWidth"
                  value={tab}
                  onChange={(e, v) => setTab(v)}
                >
                  <Tab label="Scan" />
                  <Tab label="Request" />
                  <Tab label="Monitor" />
                </Tabs>
                <TabPanel value={tab} index={0}>
                  <Grid container spacing={3}>
                    <Grid item xs>
                      <FieldText name="from" label="From ID" {...ncp} />
                    </Grid>
                    <Grid item xs>
                      <FieldText name="to" label="To ID" {...ncp} />
                    </Grid>
                    <Grid item xs>
                      <SubmitButton label="Scan MODBUS" />
                    </Grid>
                  </Grid>
                  <Alert {...alertProps} />
                  {!!scan?.addrs && <ScanResults state={state} resp={resp} />}
                </TabPanel>
                <TabPanel value={tab} index={1}>
                  <Grid container spacing={3}>
                    <Grid item xs>
                      <FieldText name="addr" label="Address" {...ncp} />
                    </Grid>
                    <Grid item xs>
                      <FieldSelect name="cmd" label="Command" {...ncp}>
                        <MenuItem value={1}>Read Coils</MenuItem>
                        <MenuItem value={2}>Read DiscreteInputs</MenuItem>
                        <MenuItem value={3}>Read Holding Registers</MenuItem>
                        <MenuItem value={4}>Read Input Registers</MenuItem>
                        <MenuItem value={5}>Write Coil</MenuItem>
                        <MenuItem value={6}>Write Holding Register</MenuItem>
                        <MenuItem value={7}>Read Exception Status</MenuItem>
                        <MenuItem value={8}>Diagnostic</MenuItem>
                        <MenuItem value={11}>Get Com Event Counter</MenuItem>
                        <MenuItem value={12}>Get Com Event Log</MenuItem>
                        <MenuItem value={15}>Write Multiple Coils</MenuItem>
                        <MenuItem value={15}>
                          Write Multiple Holding Registers
                        </MenuItem>
                      </FieldSelect>
                    </Grid>
                  </Grid>
                  <Grid container spacing={3}>
                    <Grid item xs>
                      <FieldText name="regs" label="Start reg" {...ncp} />
                    </Grid>
                    <Grid item xs>
                      <FieldText name="regc" label="Reg count" {...ncp} />
                    </Grid>
                    <Grid item xs>
                      <FieldText name="v" label="Value" {...ncp} />
                    </Grid>
                    <Grid item xs>
                      <SubmitButton label="Run request" />
                    </Grid>
                  </Grid>
                  <Alert {...alertProps} />
                </TabPanel>
              </Paper>
            </CardBox>
          );
        }}
      </FormikConsumer>
    </MuiForm>
  );
};
