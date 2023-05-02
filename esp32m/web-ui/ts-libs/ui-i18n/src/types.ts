import { TPlugin } from '@ts-libs/plugins';
import { Resource } from 'i18next';

export type Ti18nPlugin = TPlugin & {
  readonly i18n: {
    readonly resources: Resource;
  };
};
