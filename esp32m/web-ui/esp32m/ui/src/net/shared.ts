import { Lan, Construction } from '@mui/icons-material';
import component from './Nettools';
import { TContentPlugin } from '@ts-libs/ui-base';
import { Ti18nPlugin } from '@ts-libs/ui-i18n';

export const Network: TContentPlugin & Ti18nPlugin = {
  name: 'network',
  content: { title: 'Network', icon: Lan },
  i18n: {
    resources: {
      de: {
        Network: 'Netzwerk',
      },
      uk: {
        Network: 'Мережа',
      },
    },
  },
};

export const Nettools: TContentPlugin = {
  name: 'net-tools',
  use: Network,
  content: {
    title: 'Network tools',
    component,
    icon: Construction,
    menu: { parent: Network.name },
  },
};
