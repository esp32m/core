import { useState } from 'react';
import * as Yup from 'yup';
import { useFormikContext, FormikConsumer } from 'formik';
import { Button, Grid, Typography, Divider } from '@mui/material';

import { Name } from './types';
import { styled } from '@mui/material/styles';
import { useBackendApi, useModuleState } from '../../backend';
import { validators } from '../../validation';
import { FieldText, MuiForm } from '@ts-libs/ui-forms';
import { CardBox } from '@ts-libs/ui-app';

type TOptions = {
  scl?: number;
  sda?: number;
  freq?: number;
  from?: number;
  to?: number;
}

interface IScanResponse {
  ids: Array<number>;
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

const ScanButton = () => {
  const { isSubmitting, submitForm } = useFormikContext();
  return (
    <ScanDiv>
      <StyledScanButton
        variant="contained"
        disabled={isSubmitting}
        onClick={submitForm}
      >
        Scan I2C bus
      </StyledScanButton>
    </ScanDiv>
  );
};

const ScanResults = ({
  state,
  scan,
}: {
  state: TOptions;
  scan: IScanResponse;
}) => {
  const { from = 0, to = 127 } = state;
  const { ids = [] } = scan;
  const buttons = [];
  for (let i = from; i <= to; i++)
    if (ids[i >> 3] & (1 << (i & 7)))
      buttons.push(
        <Grid size={{ xs: 2 }} key={i}>
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

const ValidationSchema = Yup.object().shape({
  sda: validators.pin,
  scl: validators.pin,
  from: validators.i2cId,
  to: validators.i2cId,
});

export const Scanner = () => {
  const api = useBackendApi();
  const [scan, setScan] = useState<IScanResponse | undefined>();
  const handleSubmit = async (values: any) => {
    const resp = await api.request(Name, 'scan', values);
    setScan(resp.data);
  };
  const state = useModuleState<TOptions>(Name, { once: true }) || {};
  const ncp = {
    type: 'number',
    placeholder: 'auto',
    InputLabelProps: { shrink: true },
  };
  const haveResults = !!scan?.ids;

  return (
    <MuiForm
      initial={state}
      onSubmit={handleSubmit}
      validationSchema={ValidationSchema}
    >
      <FormikConsumer>
        {(controller) => (
          <CardBox title="I2C bus scanner" progress={controller.isSubmitting}>
            <Grid container spacing={3}>
              <FieldText name="sda" label="SDA pin" {...ncp} grid />
              <FieldText name="scl" label="SCL pin" {...ncp} grid />
              <FieldText name="freq" label="Frequency, Hz" {...ncp} grid />
            </Grid>
            <Grid container spacing={3}>
              <FieldText name="from" label="From ID" {...ncp} grid />
              <FieldText name="to" label="To ID" {...ncp} grid />
              <Grid size="grow">
                <ScanButton />
              </Grid>
            </Grid>
            {haveResults && <ScanResults state={state} scan={scan} />}
          </CardBox>
        )}
      </FormikConsumer>
    </MuiForm>
  );
};
