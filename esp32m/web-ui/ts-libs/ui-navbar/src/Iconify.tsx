import { Icon, IconifyIcon } from '@iconify/react';
import { Box, BoxProps, SxProps } from '@mui/material';

// ----------------------------------------------------------------------

interface Props extends BoxProps {
  sx?: SxProps;
  icon: IconifyIcon | string;
}

export default function Iconify({ icon, sx, ...other }: Props) {
  return <Box component={Icon} icon={icon} sx={{ ...sx }} {...other} />;
}
