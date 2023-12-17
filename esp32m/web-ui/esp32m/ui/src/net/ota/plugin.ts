import { TReduxPlugin } from '@ts-libs/redux';
import { Name, reducer } from './state';
import { Hoc } from './hoc';
import { TUiRootPlugin } from '@ts-libs/ui-base';
import { Ti18nPlugin } from '@ts-libs/ui-i18n';
import { i18n } from './i18n';

export const OtaPlugin: TReduxPlugin & TUiRootPlugin & Ti18nPlugin = {
  name: Name,
  redux: { reducer },
  i18n,
  ui: {
    root: {
      hoc: Hoc,
    },
  },
};
