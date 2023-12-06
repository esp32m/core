import { styled, useTheme } from '@mui/material/styles';
import { IconButton, AppBar, Toolbar, Typography } from '@mui/material';
import {
  cssStyles,
  useResponsive,
  useOffSetTop,
  useHeaderActions,
} from './utils';
import { MenuIcon } from './icons';

type RootStyleProps = {
  isCollapse: boolean;
  isOffset: boolean;
  verticalLayout: boolean;
};

const RootStyle = styled(AppBar, {
  shouldForwardProp: (prop) =>
    prop !== 'isCollapse' && prop !== 'isOffset' && prop !== 'verticalLayout',
})<RootStyleProps>(({ isCollapse, isOffset, verticalLayout, theme }) => ({
  ...cssStyles(theme).bgBlur(),
  boxShadow: 'none',
  height: theme.config.header.mobileHeight,
  borderBottomWidth: 1,
  borderBottomStyle: 'dotted',
  borderBottomColor: theme.palette.divider,
  zIndex: theme.zIndex.appBar + 1,
  transition: theme.transitions.create(['width', 'height'], {
    duration: theme.transitions.duration.shorter,
  }),
  [theme.breakpoints.up('lg')]: {
    height: theme.config.header.desktopHeight,
    width: `calc(100% - ${theme.config.navbar.width + 1}px)`,
    ...(isCollapse && {
      width: `calc(100% - ${theme.config.navbar.collapseWidth}px)`,
    }),
    ...(isOffset && {
      height: theme.config.header.desktopOffsetHeight,
    }),
    ...(verticalLayout && {
      width: '100%',
      height: theme.config.header.desktopOffsetHeight,
      backgroundColor: theme.palette.background.default,
    }),
  },
}));

// ----------------------------------------------------------------------

type Props = {
  onOpenSidebar: VoidFunction;
  isCollapse?: boolean;
  verticalLayout?: boolean;
};

export function Header({
  onOpenSidebar,
  isCollapse = false,
  verticalLayout = false,
}: Props) {
  const {
    config: {
      header: { desktopHeight, content },
    },
  } = useTheme();
  const isOffset = useOffSetTop(desktopHeight) && !verticalLayout;
  const actions = useHeaderActions();
  const isDesktop = useResponsive('up', 'lg');

  return (
    <RootStyle
      isCollapse={isCollapse}
      isOffset={isOffset}
      verticalLayout={verticalLayout}
    >
      <Toolbar
        sx={{
          minHeight: '100% !important',
          px: { lg: 5 },
        }}
      >
        {!isDesktop && (
          <IconButton
            onClick={onOpenSidebar}
            sx={{ mr: 1, color: 'text.primary' }}
          >
            {/*<Iconify icon="eva:menu-2-fill" /> */}
            <MenuIcon />
          </IconButton>
        )}
        {content && (
          <>
            <Typography
              noWrap
              component="h1"
              variant="h6"
              color="textSecondary"
              sx={{ flexGrow: 1 }}
            >
              {content}
            </Typography>
            {actions.length && (
              <Typography
                noWrap
                component="h1"
                variant="h6"
                color="textSecondary"
              >
                {actions}
              </Typography>
            )}
          </>
        )}
      </Toolbar>
    </RootStyle>
  );
}
