import React, { useMemo } from 'react';
import { Grid } from '@mui/material';
import { IScannerPlugin } from './types';
import { getPlugins } from '@ts-libs/plugins';

export default function Scanners(): JSX.Element {
  const plugins = getPlugins<IScannerPlugin>();
  const widgets: Array<React.ReactElement> = useMemo(
    () =>
      plugins
        .filter((p) => !!p.scanner?.component)
        .map(({ scanner }, i) =>
          React.createElement(
            scanner.component,
            Object.assign({ key: 'scanner_' + i }, scanner?.props)
          )
        ),
    [plugins]
  );
  return <Grid container>{widgets}</Grid>;
}
