import { TPlugin } from '@ts-libs/plugins';
import { TErrorFormattingOptions } from '@ts-libs/tools';
import { Resource } from 'i18next';
import { useTranslation } from 'react-i18next';

export type Ti18nPlugin = TPlugin & {
  readonly i18n: {
    readonly resources: Resource;
  };
};

export type Ti18nErrorFormattingOptions = TErrorFormattingOptions & {
  i18n?: ReturnType<typeof useTranslation>;
};
