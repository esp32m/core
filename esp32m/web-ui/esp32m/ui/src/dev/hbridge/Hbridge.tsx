import React from 'react';
import { IState, IProps, Mode } from './types';
import * as Backend from '../../backend';
import { Grid, ToggleButtonGroup, ToggleButton } from '@mui/material';
import { styled } from '@mui/material/styles';
import { useModuleState } from '../../backend';

interface IButtonsProps {
  mode: Mode;
  onChange: (m: Mode) => void;
  disabled: boolean;
}

const Title = styled(Grid)({ alignSelf: 'center' });
const StyledButtons = styled(Grid)({ marginLeft: 'auto' });

const Buttons = ({ mode, onChange, disabled }: IButtonsProps) => {
  return (
    <ToggleButtonGroup exclusive value={mode} onChange={(e, m) => onChange(m)}>
      <ToggleButton disabled={disabled} value={Mode.Off}>
        OFF
      </ToggleButton>
      <ToggleButton disabled={disabled} value={Mode.Forward}>
        Forward
      </ToggleButton>
      <ToggleButton disabled={disabled} value={Mode.Reverse}>
        Reverse
      </ToggleButton>
      <ToggleButton disabled={disabled} value={Mode.Break}>
        Break
      </ToggleButton>
    </ToggleButtonGroup>
  );
};

export default ({ name, title }:  IProps) => {
  const state=useModuleState<IState>(name);
  const [disabled, setDisabled] = React.useState(false);
  const api=Backend.useBackendApi();
  if (!state) return null;

  return (
    <Grid container>
      <Title item> {title} </Title>
      <StyledButtons item>
        <Buttons
          mode={state.mode}
          onChange={(mode: Mode) => {
            setDisabled(true);
            api.setState(name, { mode }).finally(() => setDisabled(false));
          }}
          disabled={disabled}
        />
      </StyledButtons>
    </Grid>
  );
};
