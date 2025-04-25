import {
  Box,
  Breadcrumbs,
  Grid,
  Link,
  LinkProps,
  Stack,
  Typography,
  styled,
  useTheme,
} from '@mui/material';
import { createElement, useState } from 'react';

import { Outlet } from 'react-router';
import { Header } from './Header';
import { useNavbarContext } from './context';
import { Footer } from './footer';
import { useMainMenuSections } from './utils';
import NavbarVertical from './vertical/Navbar';
import { NavLink } from 'react-router-dom';
import { useNavPath } from '@ts-libs/ui-base';

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

interface LinkRouterProps extends LinkProps {
  to: string;
  replace?: boolean;
}

function LinkRouter(props: LinkRouterProps) {
  return <Link {...props} component={NavLink} />;
}

function Crumbs() {
  const np = useNavPath();
  return (
    <Grid size="grow">
      <Breadcrumbs separator="›">
        <LinkRouter underline="hover" color="inherit" to="/">
          Home
        </LinkRouter>
        {np.segments.map((s, i) => {
          const last = i == np.segments.length - 1;
          const { name, path, label } = s;
          const inner = label ? createElement(label) : name;
          return last ? (
            <Typography color="text.primary" key={i}>
              {inner}
            </Typography>
          ) : (
            <LinkRouter underline="hover" color="inherit" to={path} key={i}>
              {inner}
            </LinkRouter>
          );
        })}
      </Breadcrumbs>
    </Grid>
  );
}
export function Layout() {
  const sections = useMainMenuSections();
  const [open, setOpen] = useState(false);
  const { isCollapse } = useNavbarContext();
  const {
    config: { breadcrumbs },
  } = useTheme();
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
          <Grid container>
            {breadcrumbs && <Crumbs />}
            <Outlet />
          </Grid>
        </MainStyle>
        <Footer />
      </Stack>
    </Box>
  );
}
