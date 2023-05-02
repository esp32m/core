import * as Yup from "yup";
import { Button, Grid, MenuItem, styled, TextField } from "@mui/material";

import { Name, IConfig, StartAction, ILocalState, IResult } from "./types";
import { useFormikContext } from "formik";
import { useDispatch } from "react-redux";
import { useSelector } from "react-redux";
import { InterfacesSelect } from "../Nettools";
import { useBackendApi, useModuleConfig } from "../../backend";
import { ConfigBox } from "../../app";
import { FieldSelect, FieldText } from "@ts-libs/ui-forms";

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
  const api = useBackendApi();
  const dispatch = useDispatch();
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
  const rows = ls?.results?.reduce((p, c) => {
    const { row, ...rd } = c;
    if (row != undefined) {
      const r = p[row] || (p[row] = []);
      r.push(rd);
    }
    return p;
  }, [] as Array<Array<IResult>>);
  const lines = rows?.map((r) => {
    const f = r[0];
    let s = `${f.ttls.toString().padStart(3, " ")}  ${f.ip?.padEnd(16, " ")}`;
    r.forEach((i) => {
      const item = i.tus ? `${Math.round(i.tus / 1000)}ms` : "*";
      s += item.padStart(10, " ");
    });
    return s;
  });
  const rtext = lines ? lines.join("\n") : " ";
  return (
    <ConfigBox
      name={Name}
      initial={config}
      title="Traceroute"
      validationSchema={ValidationSchema}
    >
      <Grid container spacing={2}>
        <Grid item xs={5}>
          <FieldText name="target" label="Host name or IP" fullWidth />
        </Grid>
        <Grid item xs={2}>
          <FieldText name="timeout" label="Timeout, ms" fullWidth />
        </Grid>
        <Grid item xs={2}>
          <FieldSelect name="proto" label="Protocol" fullWidth>
            <MenuItem value={1}>ICMP</MenuItem>
            <MenuItem value={17}>UDP</MenuItem>
          </FieldSelect>
        </Grid>
        <Grid item xs={3}>
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
        label="Traceroute results"
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
