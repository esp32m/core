import { MouseEventHandler } from 'react';

import { Button, ButtonProps } from '@mui/material';
import { styled } from '@mui/material/styles';

const Bar = styled('div')(({ theme }) => ({
  display: 'flex',
  borderRadius: `0px 0px ${theme.shape.borderRadius}px ${theme.shape.borderRadius}px`,
  width: '100%',
  marginTop: 24,
  justifyContent: 'flex-end',
  //        backgroundColor: 'rgb(250,250,250)'
}));

const Btn = styled(Button)({
  marginRight: 12,
  marginTop: 6,
  marginBottom: 6,
});

export interface IButtonProps {
  name: string;
  title?: string;
  submits?: boolean;
  cancels?: boolean;
  disabled?: boolean;
  onClick?: MouseEventHandler<any>;
}

interface IProps {
  buttons: Array<IButtonProps>;
  disabled?: boolean;
  onClick?: (name: string) => void;
  onSubmit?: (name: string) => void;
  onCancel?: (name: string) => void;
}

export function ButtonBar({
  buttons,
  disabled,
  onClick,
  onSubmit,
  onCancel,
}: IProps) {
  return (
    <Bar>
      {buttons.map((b) => {
        if (!b || !b.name) return;
        const bp: ButtonProps = {
          variant: 'contained',
          color: 'primary',
          disabled: disabled || b.disabled,
        };
        bp.onClick = (e) => {
          if (onClick) onClick(b.name);
          if (b.onClick) b.onClick(e);
          if (onSubmit && b.submits) onSubmit(b.name);
          if (onCancel && b.cancels) onCancel(b.name);
        };
        return (
          <Btn key={b.name} {...bp}>
            {b.title || b.name}
          </Btn>
        );
      })}
    </Bar>
  );
}
