import Content from './Content';
import { Network } from '../shared';
import { TContentPlugin } from '@ts-libs/ui-base';

export const NetworkInterfaces: TContentPlugin = {
  name: 'netifs',
  use: Network,
  content: {
    title: 'Interfaces',
    component: Content,
    menu: { parent: Network.name },
  },
};
