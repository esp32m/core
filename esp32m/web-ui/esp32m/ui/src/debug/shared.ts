import { BugReport } from '@mui/icons-material';

import Content from './Content';
import { TContentPlugin } from '@ts-libs/ui-base';

export const Debug: TContentPlugin = {
  name: 'debug',
  content: { title: 'Debug', icon: BugReport, component: Content },
};
