import {
  TUiThemePlugin,
  TUiLayoutPlugin,
  TUiRootPlugin,
} from '@ts-libs/ui-base';
import { ContextProvider } from './context';
import { Layout } from './layout';
import { theme } from './theme';

export const uiNavbarPlugin = (): TUiThemePlugin &
  TUiRootPlugin &
  TUiLayoutPlugin => ({
  name: 'ui-navbar',
  ui: {
    root: {
      hoc: ContextProvider,
    },
    theme,
    layout: Layout,
  },
});
