import { useState } from 'react';
import { Box, Stack, styled } from '@mui/material';

import { Outlet } from 'react-router';
import { useMainMenuSections } from './utils';
import { useNavbarContext } from './context';
import { Header } from './Header';
import NavbarVertical from './vertical/Navbar';
import { Footer } from './footer';

const MainStyle = styled('main')(({ theme }) => {
  const { collapseClick } = useNavbarContext();
  return {
    display: 'flex',
    flexGrow: 1,
    paddingTop: theme.config.header.mobileHeight + 12,
    paddingBottom: theme.config.header.mobileHeight + 12,
    [theme.breakpoints.up('lg')]: {
      paddingLeft: 16,
      paddingRight: 16,
      paddingTop: theme.config.header.desktopHeight + 12,
      paddingBottom: theme.config.header.desktopHeight + 12,
      width: `calc(100% - ${theme.config.navbar.width}px)`,
      transition: theme.transitions.create('margin-left', {
        duration: theme.transitions.duration.shorter,
      }),
      ...(collapseClick && {
        marginLeft: theme.config.navbar.collapseWidth,
      }),
    },
  };
});

/*
interface Handle {
  crumb: (arg: unknown) => ReactNode;
}
function Bcr() {
  const matches = useMatches();
  const crumbs = matches
    .filter(({ handle }) => !!(handle as Handle)?.crumb)
    .map((match) => (match.handle as Handle).crumb(match.data));

  return (
    <Breadcrumbs separator="›">
      {crumbs.map((crumb, index) => (
        <span key={index}>{crumb}</span>
      ))}
    </Breadcrumbs>
  );
}
*/
export function Layout() {
  const sections = useMainMenuSections();
  const [open, setOpen] = useState(false);
  const { isCollapse } = useNavbarContext();
  return (
    <Box
      sx={{
        display: { lg: 'flex' },
        minHeight: { lg: 1 },
      }}
    >
      <Header isCollapse={isCollapse} onOpenSidebar={() => setOpen(true)} />
      {sections && (
        <NavbarVertical
          isOpenSidebar={open}
          onCloseSidebar={() => setOpen(false)}
          menu={{ sections }}
        />
      )}
      <Stack
        sx={{
          display: { lg: 'flex' },
          flexGrow: 1,
        }}
      >
        <MainStyle>
          <Outlet />
        </MainStyle>
        <Footer />
      </Stack>
    </Box>
  );
}
