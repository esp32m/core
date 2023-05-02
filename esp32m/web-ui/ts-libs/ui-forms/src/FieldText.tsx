import { getIn, useFormikContext } from 'formik';
import { TextField, TextFieldProps } from '@mui/material';

import { FieldProps, FormValues } from './types';
import Hoc from './Hoc';
import { useTranslation } from '@ts-libs/ui-i18n';

const useField = (name: string, submitOnEnter?: boolean) => {
  const controller = useFormikContext<FormValues>();
  const {
    values,
    errors,
    touched,
    handleChange: onChange,
    handleBlur: onBlur,
    submitForm,
  } = controller;
  const wasTouched = name && getIn(touched, name);
  const errorText = name && getIn(errors, name);
  const error = !!(wasTouched && errorText);
  let value = name && getIn(values, name);
  if (value == null) value = ''; // covers null and undefined
  const p: TextFieldProps = {
    value,
    error,
    helperText: wasTouched && errorText,
    onChange,
    onBlur,
  };
  if (submitOnEnter)
    p.onKeyPress = (e) => {
      if (e.key == 'Enter') submitForm();
    };
  return p;
};

export const FieldText = (props: FieldProps<Partial<TextFieldProps>>) => {
  const { name, submitOnEnter, grid, label, ...rest } = props;
  const field = useField(name, submitOnEnter);
  const { t } = useTranslation();
  const p: TextFieldProps = {
    name,
    label: label && t(label),
    variant: 'standard',
    ...field,
    ...rest,
  };
  const hocProps = { grid };
  return (
    <Hoc {...hocProps}>
      <TextField {...p} />
    </Hoc>
  );
};
