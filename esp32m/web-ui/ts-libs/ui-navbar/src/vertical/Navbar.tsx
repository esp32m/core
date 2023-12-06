import { Box, Drawer, Stack } from '@mui/material';
import { styled, useTheme } from '@mui/material/styles';
import { useEffect } from 'react';
import { useLocation } from 'react-router-dom';
import { useNavbarContext } from '../context';
import { cssStyles, useResponsive } from '../utils';
import NavSectionVertical from './Section';

import { TNavMenu } from '../types';
import CollapseButton from './CollapseButton';

const RootStyle = styled('div')(({ theme }) => ({
  [theme.breakpoints.up('lg')]: {
    flexShrink: 0,
    transition: theme.transitions.create('width', {
      duration: theme.transitions.duration.shorter,
    }),
  },
}));

// ----------------------------------------------------------------------

type Props = {
  isOpenSidebar: boolean;
  onCloseSidebar: VoidFunction;
  menu: TNavMenu;
};

export default function NavbarVertical({
  isOpenSidebar,
  onCloseSidebar,
  menu,
}: Props) {
  const { pathname } = useLocation();

  const isDesktop = useResponsive('up', 'lg');

  const {
    isCollapse,
    collapseClick,
    collapseHover,
    onToggleCollapse,
    onHoverEnter,
    onHoverLeave,
  } = useNavbarContext();
  const theme = useTheme();
  useEffect(() => {
    if (isOpenSidebar) {
      onCloseSidebar();
    }
  }, [pathname]);

  const renderContent = (
    <Box
      sx={{
        height: 1,
        overflowX: 'hidden',
        display: 'flex',
        flexDirection: 'column',
      }}
    >
      <Stack
        spacing={3}
        sx={{
          pt: 3,
          pb: 2,
          px: 2.5,
          flexShrink: 0,
          ...(isCollapse && { alignItems: 'center' }),
        }}
      >
        <Stack
          direction="row"
          alignItems="center"
          justifyContent="space-between"
        >
          {theme.config.navbar.header}
          <Box sx={{ flexGrow: 1 }} />
          {isDesktop && !isCollapse && (
            <CollapseButton
              onToggleCollapse={onToggleCollapse}
              collapseClick={collapseClick}
            />
          )}
        </Stack>
      </Stack>

      <NavSectionVertical navConfig={menu.sections} isCollapse={isCollapse} />

      <Box sx={{ flexGrow: 1 }} />
    </Box>
  );

  return (
    <RootStyle
      sx={{
        width: {
          lg: isCollapse
            ? theme.config.navbar.collapseWidth
            : theme.config.navbar.width,
        },
        ...(collapseClick && {
          position: 'absolute',
        }),
      }}
    >
      {!isDesktop && (
        <Drawer
          open={isOpenSidebar}
          onClose={onCloseSidebar}
          PaperProps={{ sx: { width: theme.config.navbar.width } }}
        >
          {renderContent}
        </Drawer>
      )}

      {isDesktop && (
        <Drawer
          open
          variant="persistent"
          onMouseEnter={onHoverEnter}
          onMouseLeave={onHoverLeave}
          PaperProps={{
            sx: {
              width: theme.config.navbar.width,
              borderRightStyle: 'dashed',
              bgcolor: 'background.default',
              transition: (theme) =>
                theme.transitions.create('width', {
                  duration: theme.transitions.duration.standard,
                }),
              ...(isCollapse && {
                width: theme.config.navbar.collapseWidth,
              }),
              ...(collapseHover && {
                ...cssStyles(theme).bgBlur(),
                boxShadow: (theme) => theme.customShadows.z24,
              }),
            },
          }}
        >
          {renderContent}
        </Drawer>
      )}
    </RootStyle>
  );
}
