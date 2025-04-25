import React from 'react';
import { useSelector } from 'react-redux';
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
  pin?: number;
  max?: number;
}

interface IScanResponse {
  codes: Array<string>;
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
        Scan 1-wire bus
      </StyledScanButton>
    </ScanDiv>
  );
};

const ScanResults = ({ scan }: { scan: IScanResponse }) => {
  const { codes = [] } = scan;
  const buttons = [];
  for (const c of codes)
    buttons.push(
      <Grid size={{ xs: 2 }} key={c}>
        <IdButton variant="contained" color="secondary">
          {c}
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
  pin: validators.pin,
  max: Yup.number().integer().min(4).max(255),
});

export default () => {
  const api = useBackendApi()
  const owbScan = (req?: TOptions) => api.request(Name, 'scan', req);
  const handleSubmit = async (values: any) => {
    await owbScan(values);
  };
  React.useEffect(() => {
    api.getState(Name);
  }, []);
  const state = useModuleState<TOptions>(Name, { once: true }) || {};
  const scan =
    useSelector<any, IScanResponse>((state) => state.owb?.scan) || {};
  const ncp = {
    type: 'number',
    placeholder: 'auto',
    InputLabelProps: { shrink: true },
    grid: true
  };
  const haveResults = !!scan.codes;

  return (
    <MuiForm
      initial={state}
      onSubmit={handleSubmit}
      validationSchema={ValidationSchema}
    >
      <FormikConsumer>
        {(controller) => (
          <CardBox
            title="One wire bus scanner"
            progress={controller.isSubmitting}
          >
            <Grid container spacing={3}>
              <FieldText name="pin" label="Pin" {...ncp} />
              <FieldText name="max" label="Max devices" {...ncp} />
              <Grid size="grow">
                <ScanButton />
              </Grid>
            </Grid>
            {haveResults && <ScanResults scan={scan} />}
          </CardBox>
        )}
      </FormikConsumer>
    </MuiForm>
  );
};
