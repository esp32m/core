import React from 'react';
import { Grid } from '@mui/material';
import { TPlugin, getPlugins } from '@ts-libs/plugins';

type TBtPlugin = TPlugin & {
  bt: { content: React.ComponentType; props: unknown };
};

export default function Content(): JSX.Element {
  const plugins = getPlugins<TBtPlugin>();
  const widgets: Array<React.ReactElement> = React.useMemo(() => {
    return plugins
      .filter((p) => !!p.bt?.content)
      .map(({ bt }, i) =>
        React.createElement(
          bt.content,
          Object.assign({ key: 'bt' + i }, bt.props)
        )
      );
  }, [plugins]);
  return <Grid container>{widgets}</Grid>;
}
