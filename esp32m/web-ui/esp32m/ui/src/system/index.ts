import { Memory } from '@mui/icons-material';
export { SystemSummary } from './system-summary';
import { Content } from './content';
import { TContentPlugin } from '@ts-libs/ui-base';
import { Ti18nPlugin } from '@ts-libs/ui-i18n';
import { i18n } from './i18n';

export const System: TContentPlugin & Ti18nPlugin = {
  name: 'system',
  content: { title: 'System', icon: Memory, component: Content },
  i18n,
};
