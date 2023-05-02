import { Bluetooth as icon } from '@mui/icons-material';
import Content from './Content';
import { ComponentType } from 'react';
import { TContentPlugin } from '@ts-libs/ui-base';
import { TPlugin } from '@ts-libs/plugins';

export type IBluetoothPlugin = TPlugin & {
  bt: { content: ComponentType };
}

export const Bluetooth: TContentPlugin = {
  name: 'bt',
  content: { title: 'Bluetooth', icon, component: Content },
};
