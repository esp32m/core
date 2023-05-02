import React from "react";
import { Grid } from "@mui/material";
import { IDevicePlugin } from "./types";
import { getPlugins } from "@ts-libs/plugins";

export default function Content(): JSX.Element {
  const widgets: Array<React.ReactElement> = [];
  const plugins = getPlugins<IDevicePlugin>((dp) => !!dp.device);
  plugins.forEach((dp, i) => {
    if (dp.device?.component)
      widgets.push(
        React.createElement(
          dp.device.component,
          Object.assign({ key: "device_" + i }, dp.device.props)
        )
      );
  });

  return <Grid container>{widgets}</Grid>;
}
