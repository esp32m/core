import { getIn, useFormikContext } from 'formik';
import { TextField, TextFieldProps } from '@mui/material';

import { FieldProps, FormValues } from './types';
import Hoc from './Hoc';
import { useTranslation } from '@ts-libs/ui-i18n';
import { ChangeEvent } from 'react';

const useField = (props: FieldProps<Partial<TextFieldProps>>) => {
  const { name, submitOnEnter, type, select } = props;
  const controller = useFormikContext<FormValues>();
  const {
    values,
    errors,
    touched,
    handleChange,
    handleBlur: onBlur,
    submitForm,
    setFieldValue,
  } = controller;
  const wasTouched = name && getIn(touched, name);
  const errorText = name && getIn(errors, name);
  const error = !!(wasTouched && errorText);
  let value = name && getIn(values, name);
  if (value == null) value = ''; // covers null and undefined
  const onChange =
    select && type == 'number'
      ? (e: ChangeEvent<HTMLInputElement>) => {
          setFieldValue(name, Number(e.target.value));
        }
      : handleChange;

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
  const { name, submitOnEnter: _, grid, label, ...rest } = props;
  const field = useField(props);
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
