import React from "react";
import { IState, IProps, RelayState } from "./types";
import * as Backend from "../../backend";
import { Grid, ToggleButtonGroup, ToggleButton } from "@mui/material";
import { styled } from "@mui/material/styles";
import { useModuleState } from "../../backend";

interface IButtonsProps {
  state: RelayState;
  onChange: (s: RelayState) => void;
  disabled: boolean;
}

const Title = styled(Grid)({ alignSelf: "center" });
const StyledButtons = styled(Grid)({ marginLeft: "auto" });

const Buttons = ({ state, onChange, disabled }: IButtonsProps) => {
  return (
    <ToggleButtonGroup exclusive value={state} onChange={(e, s) => onChange(s)}>
      <ToggleButton disabled={disabled} value={RelayState.Off}>
        Off
      </ToggleButton>
      <ToggleButton disabled={disabled} value={RelayState.On}>
        On
      </ToggleButton>
    </ToggleButtonGroup>
  );
};

export default ({ name, title }: IProps) => {
  const state = useModuleState<IState>(name);
  const [disabled, setDisabled] = React.useState(false);
  const api = Backend.useBackendApi();
  if (!state) return null;

  return (
    <Grid container>
      <Title item> {title} </Title>
      <StyledButtons item>
        <Buttons
          state={state.state}
          onChange={(state: RelayState) => {
            setDisabled(true);
            api.setState(name, { state }).finally(() => setDisabled(false));
          }}
          disabled={disabled}
        />
      </StyledButtons>
    </Grid>
  );
};
