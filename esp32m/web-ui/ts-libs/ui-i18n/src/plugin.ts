import { TUiElementsPlugin, TUiRootPlugin } from '@ts-libs/ui-base';
import { I18nButton } from './button';
import { Hoc } from './hoc';
import { Ti18nPlugin } from './types';

export const pluginUi18n = (): TUiElementsPlugin &
  TUiRootPlugin &
  Ti18nPlugin => {
  return {
    name: 'ui-i18n',
    ui: {
      header: {
        actions: [I18nButton],
      },
      root: {
        hoc: Hoc,
      },
    },
    i18n: {
      resources: {
        en: {},
        de: {
          yes: 'Ja',
          no: 'Nein',
          cancel: 'Abbrechen',
          continue: 'Weiter',
          Username: 'Benutzername',
          Password: 'Passwort',
          'less than a second': 'weniger als eine Sekunde',
          ago: 'Vor',
          english: 'Englisch',
          german: 'Deutsch',
          ukrainian: 'Ukrainisch',
          italian: 'Italienisch',
        },
        uk: {
          yes: 'так',
          no: 'ні',
          cancel: 'відміна',
          continue: 'продовжити',
          Username: 'Користувач',
          Password: 'Пароль',
          'less than a second': 'менше секунди',
          ago: 'тому',
          english: 'англійська',
          german: 'німецька',
          ukrainian: 'українська',
          italian: 'італійська',
        },
        it: {
          yes: 'sì',
          no: 'no',
          cancel: 'annulla',
          continue: 'continua',
          Username: 'Nome utente',
          Password: 'Password',
          'less than a second': 'meno di un secondo',
          ago: 'fa',
          english: 'inglese',
          german: 'tedesco',
          ukrainian: 'ucraino',
          italian: 'italiano',
        },
      },
    },
  };
};
