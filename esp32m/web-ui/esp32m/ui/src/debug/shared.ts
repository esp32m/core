import { BugReport } from '@mui/icons-material';

import { content } from './content';
import { TContentPlugin } from '@ts-libs/ui-base';

export const Debug: TContentPlugin = {
  name: 'debug',
  content: { title: 'Debug', icon: BugReport, component: content },
};
