import React from "react";
import { TSlaveState, TProps } from "./types";
import * as Backend from "../../backend";
import {  useModuleState } from "../..";
import { Slider, Grid } from "@mui/material";
import { CardBox } from "@ts-libs/ui-app";

export default ({ name, title }: TProps) => {
  const state = useModuleState<TSlaveState>(name);
  const [disabled, setDisabled] = React.useState(false);
  const api = Backend.useBackendApi();
  if (!state) return null;

  return (
    <CardBox title={title || name}>
      <Grid container>
        <Grid size={{ xs: 12 }}>
          <Slider
            min={state.hvac.bounds?.ch?.[0] || 0}
            max={state.hvac.bounds?.ch?.[1] || 100}
            valueLabelDisplay="auto"
            value={state.hvac.maxts}
            onChange={(event, maxts) => {
              setDisabled(true);
              api
                .setState(name, { hvac: { maxts } })
                .finally(() => setDisabled(false));
            }}
            disabled={disabled}
          />
        </Grid>
      </Grid>
    </CardBox>
  );
};
