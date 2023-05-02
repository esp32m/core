import { PropsWithChildren, useEffect, useState } from 'react';
import { I18nextProvider } from 'react-i18next';

import i18n, {
  BackendModule,
  InitOptions,
  ReadCallback,
  Services,
  TOptions,
} from 'i18next';
import LanguageDetector from 'i18next-browser-languagedetector';
import { initReactI18next } from 'react-i18next';
import { getBundles, getLanguages } from './utils';
import { isDevelopment } from '@ts-libs/tools';

class Loader implements BackendModule {
  readonly type = 'backend';
  init(
    services: Services,
    backendOptions: TOptions,
    i18nextOptions: InitOptions
  ) {}
  read(language: string, namespace: string, callback: ReadCallback) {
    const bundles = getBundles();
    callback(null, bundles[language]);
  }
}

const fallbackLng = 'en';

let instance: typeof i18n | undefined;
function init() {
  if (!instance) {
    const i = (instance = i18n
      .use(new Loader())
      .use(LanguageDetector)
      .use(initReactI18next)); // bind react-i18next to the instance
    i.init({
      fallbackLng,
      partialBundledLanguages: true,
      missingKeyNoValueFallbackToKey: true,
      debug: isDevelopment(),
      resources: {},
      interpolation: {
        escapeValue: false, // not needed for react!!
      },
    }).then(() => {
      const languages = new Set(getLanguages().map((l) => l.toLowerCase()));
      let { language = fallbackLng } = i;
      language = language.toLowerCase();
      if (!languages.has(language)) {
        let newLanguage = fallbackLng;
        const parts = language.split('-');
        for (const p of parts)
          if (languages.has(p)) {
            newLanguage = p;
            break;
          }
        i.changeLanguage(newLanguage);
      }
    });
  }
  return instance;
}

export const Hoc = ({ children }: PropsWithChildren) => {
  const [value, setValue] = useState<typeof i18n | undefined>();
  useEffect(() => {
    setValue(init());
  }, []);
  if (!value) return null;
  return <I18nextProvider i18n={value}>{children}</I18nextProvider>;
};
