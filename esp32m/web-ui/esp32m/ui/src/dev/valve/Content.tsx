import React from 'react';
import {
  Alert,
  Button,
  Grid,
  ToggleButton,
  ToggleButtonGroup,
} from '@mui/material';
import { TState, TProps, ValveState } from './types';
import * as Backend from '../../backend';
import { useModuleState } from '../..';
import { styled } from '@mui/material/styles';
import { CardBox } from '@ts-libs/ui-app';

interface IButtonsProps {
  state: ValveState;
  onChange: (s: ValveState) => void;
  disabled: boolean;
}

const Title = styled(Grid)({ alignSelf: 'center' });
const StyledButtons = styled(Grid)({ marginLeft: 'auto' });

const Buttons = ({ state, onChange, disabled }: IButtonsProps) => {
  return (
    <ToggleButtonGroup
      exclusive
      value={state}
      onChange={(e, s) => s && onChange(s)}
    >
      <ToggleButton disabled={disabled} value={ValveState.Closed}>
        Close
      </ToggleButton>
      <ToggleButton disabled={disabled} value={ValveState.Open}>
        Open
      </ToggleButton>
    </ToggleButtonGroup>
  );
};

export default ({ name, title }: TProps) => {
  const state = useModuleState<TState>(name);
  const [disabled, setDisabled] = React.useState(false);
  const api = Backend.useBackendApi();
  if (!state) return null;
  const command = (state: string) => {
    setDisabled(true);
    api.setState(name, { state }).finally(() => setDisabled(false));
  };

  return (
    <CardBox title={title || name}>
      {state.stopPending && (
        <Alert severity="error" sx={{ mb: 1 }}>
          Motor stop is not confirmed. Retrying automatically.
          {state.stopErrorName && ` ${state.stopErrorName}`}
        </Alert>
      )}
      {state.fault && (
        <Alert severity="warning" sx={{ mb: 1 }}>
          Valve fault: {state.fault} ({state.errorName || state.error}).
          {!state.stopPending && ' A new command is required to resume.'}
        </Alert>
      )}
      <Grid container>
        <Title> {`State: ${state.state}`} </Title>
        <StyledButtons>
          <Button disabled={disabled} onClick={() => command('stop')}>
            Stop
          </Button>
          <Buttons
            state={state.state}
            onChange={command}
            disabled={disabled || !!state.stopPending}
          />
        </StyledButtons>
      </Grid>
    </CardBox>
  );
};
