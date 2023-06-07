import { TReduxPlugin } from '@ts-libs/redux';
import { TUiRootPlugin } from '@ts-libs/ui-base';
import { Snacks } from './snacks';
import { Name, reducer } from './state';

export const pluginUiSnack = (): TReduxPlugin & TUiRootPlugin => ({
  name: Name,
  redux: { reducer },
  ui: {
    root: {
      leaf: Snacks,
    },
  },
});
