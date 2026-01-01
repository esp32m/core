import { Terminal as icon } from '@mui/icons-material';
import { Network } from '../shared';
import { Content } from './content';
import { Name } from './types';
import { TContentPlugin } from '@ts-libs/ui-base';

export const UartTerm = (): TContentPlugin => ({
    name: Name,
    use: Network,
    content: {
        title: 'UART terminal',
        icon,
        component: Content,
        menu: { parent: Network.name },
    },
});
