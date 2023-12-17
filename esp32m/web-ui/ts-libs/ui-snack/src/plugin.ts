import { TReduxPlugin } from '@ts-libs/redux';
import { TUiRootPlugin } from '@ts-libs/ui-base';
import { Snacks } from './snacks';
import { Name, reducer } from './state';
import { pluginUi18n } from '@ts-libs/ui-i18n';

export const pluginUiSnack = (): TReduxPlugin & TUiRootPlugin => ({
  name: Name,
  redux: { reducer },
  use: pluginUi18n,
  ui: {
    root: {
      leaf: Snacks,
    },
  },
});
