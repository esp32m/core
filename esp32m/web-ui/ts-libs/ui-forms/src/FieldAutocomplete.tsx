import { getIn, useFormikContext } from 'formik';
import { TextField, Autocomplete, TextFieldProps } from '@mui/material';
import { FormValues, FieldProps } from './types';
import { useTranslation } from '@ts-libs/ui-i18n';

export const FieldAutocomplete = (
  props: FieldProps<TextFieldProps> & { autocompleteProps: any }
) => {
  const { name, label, autocompleteProps, ...rest } = props;
  const { t } = useTranslation();
  const controller = useFormikContext<FormValues>();
  const {
    values,
    errors,
    touched,
    handleChange: onChange,
    handleBlur: onBlur,
  } = controller;
  const error = name && !!(getIn(errors, name) && getIn(touched, name));
  let value = name && getIn(values, name);
  if (value === undefined) value = '';
  const tp = {
    error,
    label: t(label),
    name,
    onBlur,
    helperText: error && errors[name],
    ...rest,
  };

  const p = {
    value,
    onChange,
    renderInput: (params: any) => <TextField {...params} {...tp} />,
    ...autocompleteProps,
  };
  return <Autocomplete {...p} />;
};
