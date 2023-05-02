import * as Yup from "yup";
import { Button, Grid, styled, TextField } from "@mui/material";

import { Name, IConfig, StartAction, ILocalState } from "./types";
import { useFormikContext } from "formik";
import { useDispatch } from "react-redux";
import { useSelector } from "react-redux";
import { InterfacesSelect } from "../Nettools";
import { useBackendApi, useModuleConfig } from "../../backend";
import { ConfigBox } from "../../app";
import { FieldText } from "@ts-libs/ui-forms";

const StyledScanButton = styled(Button)({
  marginRight: 12,
  marginTop: 6,
  marginBottom: 6,
});
const ActionDiv = styled("div")({
  display: "flex",
  width: "100%",
  marginTop: 24,
  justifyContent: "flex-end",
});

const ActionButton = ({ action }: { action: string }) => {
  const { isSubmitting, submitForm, dirty } = useFormikContext();
  const dispatch = useDispatch();
  const api = useBackendApi();
  const onClick = async () => {
    if (dirty) await submitForm();
    dispatch({ type: StartAction });
    api.request(Name, action);
  };
  return (
    <StyledScanButton
      variant="contained"
      disabled={isSubmitting}
      onClick={onClick}
    >
      {action}
    </StyledScanButton>
  );
};

const ValidationSchema = Yup.object().shape({});

export default () => {
  const ls = useSelector<any>((s) => s[Name]) as ILocalState;
  const [config] = useModuleConfig<IConfig>(Name);
  if (!config) return null;
  const lines = ls?.results?.map((r) => {
    let s = "";
    switch (r.status) {
      case 0:
        s = `Reply from ${r.addr}: bytes=${r.bytes} time=${r.time}ms TTL=${r.ttl}`;
        break;
      case 1:
        s = `Request timed out.`;
        break;
      case 2:
        s = `Sent: ${r.tx}, received: ${r.rx}, lost: ${
          r.tx - r.rx
        } (${Math.round(((r.tx - r.rx) * 100) / r.tx)}% lost)`;
        break;
    }
    return s;
  });
  const rtext = lines ? lines.join("\n") : " ";
  return (
    <ConfigBox
      name={Name}
      initial={config}
      title="Ping"
      validationSchema={ValidationSchema}
    >
      <Grid container spacing={2}>
        <Grid item xs={6}>
          <FieldText name="target" label="Host name or IP" fullWidth />
        </Grid>
        <Grid item xs={2}>
          <FieldText name="count" label="Count" fullWidth />
        </Grid>
        <Grid item xs={2}>
          <FieldText name="interval" label="Interval, ms" fullWidth />
        </Grid>
        <Grid item xs={2}>
          <FieldText name="size" label="Size, bytes" fullWidth />
        </Grid>
        <Grid item xs={3}>
          <FieldText name="ttl" label="Time to live" fullWidth />
        </Grid>
        <Grid item xs={3}>
          <FieldText name="timeout" label="Timeout, ms" fullWidth />
        </Grid>
        <Grid item xs={6}>
          <InterfacesSelect interfaces={config.interfaces} />
        </Grid>
      </Grid>
      <ActionDiv>
        <ActionButton action="start" />
        <ActionButton action="stop" />
      </ActionDiv>
      <TextField
        fullWidth
        multiline
        rows={10}
        variant="outlined"
        label="Ping results"
        value={rtext}
        style={{ marginTop: "8px" }}
        InputProps={{
          style: {
            fontFamily: "monospace",
            overflow: "auto",
            whiteSpace: "nowrap",
          },
        }}
      ></TextField>
    </ConfigBox>
  );
};
