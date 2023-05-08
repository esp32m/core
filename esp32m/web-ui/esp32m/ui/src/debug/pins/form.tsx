import { Button, Grid } from '@mui/material';
import { Formik, useFormikContext } from 'formik';
import { PropsWithChildren } from 'react';
import { useBackendApi } from '../../backend';
import { Name, selectors } from './state';
import { useSelector } from 'react-redux';

import * as Yup from 'yup';

const ValidationSchema = Yup.object().shape({
  freq: Yup.number().integer().min(0).max(1000000),
});

const Buttons = () => {
  const { handleSubmit, dirty, isSubmitting } = useFormikContext();
  return (
    <Grid item xs>
      <Button
        disabled={!dirty && !isSubmitting}
        sx={{ float: 'right', clear: 'right' }}
        onClick={() => handleSubmit()}
      >
        Update
      </Button>
    </Grid>
  );
};

type TFormProps = {
  state: any;
  onChange: () => Promise<unknown>;
};

export const Form = ({
  children,
  state,
  onChange,
}: PropsWithChildren<TFormProps>) => {
  const pin = useSelector(selectors.pin);
  const feature = useSelector(selectors.feature);
  const api = useBackendApi();
  const handleSubmit = async (values: any) => {
    await api.setState(Name, { pin, feature, state: values });
    await onChange();
  };
  return (
    <Formik
      onSubmit={handleSubmit}
      initialValues={state}
      enableReinitialize
      validationSchema={ValidationSchema}
    >
      <>
        {children}
        <Buttons />
      </>
    </Formik>
  );
};
