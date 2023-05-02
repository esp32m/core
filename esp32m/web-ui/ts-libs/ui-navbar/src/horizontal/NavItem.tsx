import { ReactElement, forwardRef } from 'react';
import { NavLink as RouterLink } from 'react-router-dom';
import { Box, Link, useTheme } from '@mui/material';
import { NavItemProps } from '../types';

import Iconify from '../Iconify';
import { ListItemStyle } from './style';
import { isExternalLink } from '../utils';

export const NavItemRoot = forwardRef<
  HTMLButtonElement & HTMLAnchorElement,
  NavItemProps
>(({ item, active, open, onMouseEnter, onMouseLeave }, ref) => {
  const { title, path, icon, children } = item;

  if (children) {
    return (
      <ListItemStyle
        ref={ref}
        open={open}
        activeRoot={active}
        onMouseEnter={onMouseEnter}
        onMouseLeave={onMouseLeave}
      >
        <NavItemContent icon={icon} title={title}>
          {children}
        </NavItemContent>
      </ListItemStyle>
    );
  }

  return isExternalLink(path) ? (
    <ListItemStyle component={Link} href={path} target="_blank" rel="noopener">
      <NavItemContent icon={icon} title={title}>
        {children}
      </NavItemContent>
    </ListItemStyle>
  ) : (
    <ListItemStyle component={RouterLink} to={path} activeRoot={active}>
      <NavItemContent icon={icon} title={title}>
        {children}
      </NavItemContent>
    </ListItemStyle>
  );
});
NavItemRoot.displayName = 'NavItemRoot';

// ----------------------------------------------------------------------

export const NavItemSub = forwardRef<
  HTMLButtonElement & HTMLAnchorElement,
  NavItemProps
>(({ item, active, open, onMouseEnter, onMouseLeave }, ref) => {
  const { title, path, icon, children } = item;

  if (children) {
    return (
      <ListItemStyle
        ref={ref}
        subItem
        disableRipple
        open={open}
        activeSub={active}
        onMouseEnter={onMouseEnter}
        onMouseLeave={onMouseLeave}
      >
        <NavItemContent icon={icon} title={title} subItem>
          {children}
        </NavItemContent>
      </ListItemStyle>
    );
  }

  return isExternalLink(path) ? (
    <ListItemStyle
      subItem
      href={path}
      disableRipple
      rel="noopener"
      target="_blank"
      component={Link}
    >
      <NavItemContent icon={icon} title={title} subItem>
        {children}
      </NavItemContent>
    </ListItemStyle>
  ) : (
    <ListItemStyle
      disableRipple
      component={RouterLink}
      to={path}
      activeSub={active}
      subItem
    >
      <NavItemContent icon={icon} title={title} subItem>
        {children}
      </NavItemContent>
    </ListItemStyle>
  );
});
NavItemSub.displayName = 'NavItemSub';

// ----------------------------------------------------------------------

type NavItemContentProps = {
  title: string;
  icon?: ReactElement;
  children?: { title: string; path: string }[];
  subItem?: boolean;
};

function NavItemContent({
  icon,
  title,
  children,
  subItem,
}: NavItemContentProps) {
  const { config } = useTheme();
  const size = config.icon.sizeHorizontal;
  return (
    <>
      {icon && (
        <Box
          component="span"
          sx={{
            mr: 1,
            width: size,
            height: size,
            '& svg': { width: '100%', height: '100%' },
          }}
        >
          {icon}
        </Box>
      )}
      {title}
      {children && (
        <Iconify
          icon={subItem ? 'eva:chevron-right-fill' : 'eva:chevron-down-fill'}
          sx={{
            ml: 0.5,
            width: size,
            height: size,
          }}
        />
      )}
    </>
  );
}
