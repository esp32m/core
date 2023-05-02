import { DevicesOther } from '@mui/icons-material';
import Content from './Content';
import { TContentPlugin } from '@ts-libs/ui-base';

export const Devices: TContentPlugin = {
  name: 'devices',
  content: { title: 'Devices', icon: DevicesOther, component: Content },
};
