import { CssBaseline } from '@mui/material';
import {
  StyledEngineProvider,
  ThemeOptions,
  ThemeProvider,
  createTheme,
} from '@mui/material/styles';
import { getPlugins } from '@ts-libs/plugins';
import { merge } from '@ts-libs/tools';
import { TUiThemePlugin } from '@ts-libs/ui-base';
import React, { useMemo } from 'react';
import ReactIs from 'react-is';

export const ThemeRoot = ({ children }: React.PropsWithChildren<unknown>) => {
  const plugins = getPlugins<TUiThemePlugin>();
  const themeOptions: ThemeOptions = useMemo(
    () =>
      plugins
        .filter((p) => !!p.ui?.theme)
        .reverse()
        .reduce(
          (p, c) =>
            merge([p, c.ui.theme], { isLeaf: (el) => ReactIs.isElement(el) }),
          {} as ThemeOptions
        ),
    [plugins]
  );

  const theme = createTheme(themeOptions);

  return (
    <StyledEngineProvider injectFirst>
      <ThemeProvider theme={theme}>
        <CssBaseline />
        {children}
      </ThemeProvider>
    </StyledEngineProvider>
  );
};
