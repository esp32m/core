import React from 'react';
import { TState, IProps, Mode, TConfig } from './types';
import * as Backend from '../../backend';
import {
  Grid,
  ToggleButtonGroup,
  ToggleButton,
  IconButton,
} from '@mui/material';
import { styled } from '@mui/material/styles';
import { useModuleConfig, useModuleState } from '../../backend';
import { DialogForm, FieldText, useDialogForm } from '@ts-libs/ui-forms';
import { useSnackApi } from '@ts-libs/ui-snack';
import { Settings } from '@mui/icons-material';
import * as Yup from 'yup';

const validationSchema = Yup.object().shape({
  speed: Yup.number().min(0).max(1),
});

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
      <ToggleButton disabled={disabled} value={Mode.Brake}>
        Brake
      </ToggleButton>
    </ToggleButtonGroup>
  );
};

export const Hbridge = ({ name, title }: IProps) => {
  const state = useModuleState<TState>(name);
  const [config, refresh] = useModuleConfig<TConfig>(name);
  const snackApi = useSnackApi();
  const [hook, openSettings] = useDialogForm({
    initialValues: config || {},
    onSubmit: async (v) => {
      try {
        await api.setConfig(name, v);
      } catch (e) {
        snackApi.error(e);
      } finally {
        refresh();
      }
    },
    validationSchema,
  });

  const [disabled, setDisabled] = React.useState(false);
  const api = Backend.useBackendApi();
  if (!state) return null;

  return (
    <Grid container>
      <Title item> {title} </Title>
      <StyledButtons item>
        <Buttons
          mode={state.mode}
          onChange={async (mode: Mode) => {
            setDisabled(true);
            try {
              await api.setState(name, { mode });
            } catch (e) {
              snackApi.error(e);
            } finally {
              setDisabled(false);
            }
          }}
          disabled={disabled}
        />
      </StyledButtons>
      {state.current && state.current.toFixed(1)}
      <IconButton onClick={() => openSettings()}>
        <Settings />
      </IconButton>
      {config && (
        <DialogForm title="Configure H-Bridge" hook={hook}>
          <Grid container spacing={3}>
            <FieldText name="speed" label="Speed" grid />
          </Grid>
        </DialogForm>
      )}
    </Grid>
  );
};
