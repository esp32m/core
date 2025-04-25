import React, { useMemo } from 'react';
import { Grid, MenuItem } from '@mui/material';
import { INettoolsPlugin, TNetInterface, TNetInterfaces } from './types';
import { getPlugins } from '@ts-libs/plugins';
import { FieldSelect } from '@ts-libs/ui-forms';

export default () => {
  const plugins = getPlugins<INettoolsPlugin>();
  const widgets: Array<React.ReactElement> = useMemo(
    () =>
      plugins
        .filter((p) => !!p.nettools?.content)
        .map(({ nettools }, i) =>
          React.createElement(
            nettools.content,
            Object.assign({ key: 'nettools_' + i }, nettools.props)
          )
        ),
    [plugins]
  );
  return <Grid container>{widgets}</Grid>;
};

const toMenuItem = (e: TNetInterface, i: number) => (
  <MenuItem key={i} value={e[2]}>
    {`${e[0]} (${e[1]})`}
  </MenuItem>
);

export const InterfacesSelect = ({
  interfaces,
}: {
  interfaces: TNetInterfaces;
}) => {
  return (
    <FieldSelect name="iface" label="Interface" fullWidth>
      <MenuItem value={0}>Default interface</MenuItem>
      {interfaces.map(toMenuItem)}
    </FieldSelect>
  );
};
