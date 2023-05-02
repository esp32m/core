import { FieldProps } from './types';
import { FieldText } from './FieldText';
import { TextFieldProps } from '@mui/material';

export const FieldSelect = (props: FieldProps<TextFieldProps>) => {
  const p = { ...props, select: true };
  return <FieldText {...p} />;
};
