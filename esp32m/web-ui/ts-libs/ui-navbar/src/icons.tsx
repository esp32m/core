import { Box, BoxProps, SvgIcon, SxProps } from '@mui/material';

const Menu = (
  <svg
    xmlns="http://www.w3.org/2000/svg"
    width="24"
    height="24"
    viewBox="0 0 24 24"
  >
    <g fill="currentColor">
      <circle cx="4" cy="12" r="1" />
      <rect width="14" height="2" x="7" y="11" rx=".94" ry=".94" />
      <rect width="18" height="2" x="3" y="16" rx=".94" ry=".94" />
      <rect width="18" height="2" x="3" y="6" rx=".94" ry=".94" />
    </g>
  </svg>
);

interface Props extends BoxProps {
  sx?: SxProps;
  icon: JSX.Element;
}

const I = ({ icon, sx, ...other }: Props) => (
  <Box component={SvgIcon} sx={{ ...sx }} {...other}>
    {icon}
  </Box>
);

export const MenuIcon = (props?: BoxProps & { sx?: SxProps }) => (
  <I icon={Menu} {...props} />
);
