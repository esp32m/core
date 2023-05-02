import { Scanner } from '@mui/icons-material';
import component from './Scanners';
import { TContentPlugin } from '@ts-libs/ui-base';

export const Scanners: TContentPlugin = {
  name: 'bus-scanners',
  content: { title: 'Bus scanners', icon: Scanner, component },
};
