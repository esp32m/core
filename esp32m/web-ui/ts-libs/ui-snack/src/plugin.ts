import { TReduxPlugin } from '@ts-libs/redux';
import { Name, reducer } from './state';
import { TUiRootPlugin } from '@ts-libs/ui-base';
import { Snacks } from './snacks';

export const uiSnackPlugin = (): TReduxPlugin & TUiRootPlugin => ({
  name: Name,
  redux: { reducer },
  ui: {
    root: {
      leaf: Snacks,
    },
  },
});
