import { GridProps } from '@mui/material';
import { FormikValues, FormikContextType } from 'formik';
import { FormikHelpers } from 'formik/dist/types';

export type FormValues = FormikValues;
export type IController<Values = FormValues> = FormikContextType<Values>;
export type SubmitHandler = (
  values: FormikValues,
  formikHelpers: FormikHelpers<FormikValues>
) => void | Promise<unknown>;

export type FieldProps<T> = T & {
  name: string;
  label: string;
  submitOnEnter?: boolean;
  grid?: GridProps | boolean;
};
