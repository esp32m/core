import { Formik, Form, FormikConfig, FormikValues, isFunction } from 'formik';
import { SubmitHandler } from './types';

interface IProps extends Partial<FormikConfig<FormikValues>> {
  initial: FormikValues;
  onSubmit: SubmitHandler;
}

export const MuiForm = (props: IProps) => {
  const { children, initial, onSubmit, ...other } = props;
  const fp = {
    initialValues: initial,
    enableReinitialize: true,
    onSubmit,
    ...other,
  };
  return (
    <Formik {...fp}>
      {(controller) => (
        <Form>{isFunction(children) ? children(controller) : children}</Form>
      )}
    </Formik>
  );
};
