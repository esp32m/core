import { Button, Grid } from '@mui/material';
import { Formik, useFormikContext } from 'formik';
import { useBackendApi } from '../../backend';
import { Name, selectors } from './state';
import { useSelector } from 'react-redux';
import { useFeature } from './hooks';

const Buttons = () => {
  const { submitForm, resetForm, dirty, isSubmitting } = useFormikContext();
  return (
    <Grid item xs>
      <Button
        disabled={!dirty && !isSubmitting}
        sx={{ float: 'right', clear: 'right' }}
        onClick={() => submitForm().then(() => resetForm())}
      >
        Update
      </Button>
    </Grid>
  );
};

type TFormProps = {
  onChange: () => Promise<unknown>;
};

export const Form = ({ onChange }: TFormProps) => {
  const pin = useSelector(selectors.pin);
  const feature = useFeature();
  const pinState = useSelector(selectors.pinState);
  const api = useBackendApi();
  const handleSubmit = async (values: any) => {
    if (feature.info?.beforeSubmit) values = feature.info.beforeSubmit(values);
    await api.setState(Name, { pin, feature: feature?.feature, state: values });
    await onChange();
  };
  if (!pinState) return null;
  const FC = feature.info?.component;
  if (!FC) return null;
  return (
    <Formik
      onSubmit={handleSubmit}
      initialValues={pinState.state}
      enableReinitialize
      validationSchema={feature.info?.validationSchema}
    >
      <>
        <FC />
        <Buttons />
      </>
    </Formik>
  );
};
