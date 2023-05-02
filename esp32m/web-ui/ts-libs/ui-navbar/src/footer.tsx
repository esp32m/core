import { styled, useTheme, Box, Typography } from '@mui/material';
import { cssStyles } from './utils';
import { isString } from '@ts-libs/tools';
import { createElement } from 'react';

const RootStyle = styled(Box)(({ theme }) => ({
  ...cssStyles(theme).bgBlur(),
  boxShadow: 'none',
  position: 'fixed',
  bottom: 0,
  borderTopWidth: 1,
  borderTopStyle: 'dotted',
  borderTopColor: theme.palette.divider,
  textAlign: 'center',
  padding: 4,
  zIndex: theme.zIndex.appBar + 1,
  width: '100%',
  transition: theme.transitions.create(['width', 'height'], {
    duration: theme.transitions.duration.shorter,
  }),
  [theme.breakpoints.up('lg')]: {
    width: `calc(100% - ${theme.config.navbar.width + 1}px)`,
  },
}));

export const Footer = () => {
  const theme = useTheme();
  const footer = theme.config.footer.content;
  if (!footer) return null;
  const content = isString(footer) ? (
    <Typography variant="caption">{footer}</Typography>
  ) : (
    createElement(footer)
  );

  return <RootStyle>{content}</RootStyle>;
};
