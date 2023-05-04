import { TReduxPlugin } from '@ts-libs/redux';
import { Name, reducer } from './state';
import { Hoc } from './hoc';
import { TUiRootPlugin } from '@ts-libs/ui-base';

export const OtaPlugin: TReduxPlugin & TUiRootPlugin = {
  name: Name,
  redux: { reducer },
  ui: {
    root: {
      hoc: Hoc,
    },
  },
};
