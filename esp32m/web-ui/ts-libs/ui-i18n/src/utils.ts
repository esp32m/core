import { Resource } from 'i18next';
import { Ti18nPlugin } from './types';
import { TPlugin, getPlugins } from '@ts-libs/plugins';
import { merge } from '@ts-libs/tools';

export const LanguageNames: Record<string, string> = {
  en: 'english',
  de: 'german',
  uk: 'ukrainian',
};

export const getBundles = (() => {
  const cache = {
    plugins: [] as Array<TPlugin>,
    bundles: {} as Resource,
  };
  return () => {
    const plugins = getPlugins<Ti18nPlugin>();
    if (cache.plugins !== plugins) {
      cache.plugins = plugins;
      cache.bundles = plugins.reduce((m, p) => {
        if (p.i18n?.resources) m = merge([m, p.i18n?.resources]);
        return m;
      }, {} as Resource);
    }
    return cache.bundles;
  };
})();

export const getLanguages = () =>
  Array.from(new Set(Object.keys(getBundles())));
