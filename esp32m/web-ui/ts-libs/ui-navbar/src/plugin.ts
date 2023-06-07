import {
    TUiLayoutPlugin,
    TUiRootPlugin,
    TUiThemePlugin,
} from '@ts-libs/ui-base';
import { pluginUi18n } from '@ts-libs/ui-i18n';
import { ContextProvider } from './context';
import { Layout } from './layout';
import { theme } from './theme';

export const pluginUiNavbar = (): TUiThemePlugin &
  TUiRootPlugin &
  TUiLayoutPlugin => ({
  name: 'ui-navbar',
  use: pluginUi18n(),
  ui: {
    root: {
      hoc: ContextProvider,
    },
    theme,
    layout: Layout,
  },
});
