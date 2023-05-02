import { FieldProps } from './types';
import { FieldText } from './FieldText';
import { IconButton, InputAdornment, TextFieldProps } from '@mui/material';
import { useState } from 'react';
import { Visibility, VisibilityOff } from '@mui/icons-material';

const VisibilitySwitch = ({
  showPass,
  setShowPass,
}: {
  showPass: boolean;
  setShowPass: (b: boolean) => void;
}) => (
  <InputAdornment position="end">
    <IconButton
      aria-label="Toggle password visibility"
      onClick={() => setShowPass(!showPass)}
    >
      {showPass ? <Visibility /> : <VisibilityOff />}
    </IconButton>
  </InputAdornment>
);

export const FieldPassword = (
  props: FieldProps<TextFieldProps> & { revealable?: boolean }
) => {
  const [showPass, setShowPass] = useState(false);
  const { revealable, ...rest } = props;
  const p: FieldProps<TextFieldProps> = {
    type: showPass ? 'text' : 'password',
    autoComplete: 'off',
    InputProps: {
      endAdornment: revealable && (
        <VisibilitySwitch showPass={showPass} setShowPass={setShowPass} />
      ),
    },
    ...rest,
  };
  return <FieldText {...p} />;
};
