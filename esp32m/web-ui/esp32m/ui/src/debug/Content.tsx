import React, { useMemo } from 'react';
import { Grid } from '@mui/material';
import { TDebugPlugin } from './types';
import { getPlugins } from '@ts-libs/plugins';

export default () => {
  const plugins = getPlugins<TDebugPlugin>();
  const widgets: Array<React.ReactElement> = useMemo(
    () =>
      plugins
        .filter((p) => !!p.debug?.content)
        .map(({ debug }, i) =>
          React.createElement(
            debug.content,
            Object.assign({ key: 'debug_' + i }, debug.props)
          )
        ),
    [plugins]
  );
  return <Grid container>{widgets}</Grid>;
};
