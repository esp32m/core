import { ElementType } from 'react';
import { alpha, styled } from '@mui/material/styles';
import {
  LinkProps,
  ListItemText,
  ListItemButton,
  ListItemIcon,
  ListItemButtonProps,
} from '@mui/material';

// ----------------------------------------------------------------------

type IProps = LinkProps & ListItemButtonProps;

interface ListItemStyleProps extends IProps {
  component?: ElementType;
  to?: string;
  activeRoot?: boolean;
  activeSub?: boolean;
  subItem?: boolean;
}

export const ListItemStyle = styled(ListItemButton, {
  shouldForwardProp: (prop) =>
    prop !== 'activeRoot' && prop !== 'activeSub' && prop !== 'subItem',
})<ListItemStyleProps>(({ activeRoot, activeSub, subItem, theme }) => ({
  ...theme.typography.body2,
  position: 'relative',
  height: theme.config.navbar.itemRootHeight,
  textTransform: 'capitalize',
  paddingLeft: theme.spacing(2),
  paddingRight: theme.spacing(1.5),
  marginBottom: theme.spacing(0.5),
  color: theme.palette.text.secondary,
  borderRadius: theme.shape.borderRadius,
  // activeRoot
  ...(activeRoot && {
    ...theme.typography.subtitle2,
    color: theme.palette.primary.main,
    backgroundColor: alpha(
      theme.palette.primary.main,
      theme.palette.action.selectedOpacity
    ),
  }),
  // activeSub
  ...(activeSub && {
    ...theme.typography.subtitle2,
    color: theme.palette.text.primary,
  }),
  // subItem
  ...(subItem && {
    height: theme.config.navbar.itemSubHeight,
  }),
}));

interface ListItemTextStyleProps extends ListItemButtonProps {
  isCollapse?: boolean;
}

export const ListItemTextStyle = styled(ListItemText, {
  shouldForwardProp: (prop) => prop !== 'isCollapse',
})<ListItemTextStyleProps>(({ isCollapse, theme }) => ({
  whiteSpace: 'nowrap',
  transition: theme.transitions.create(['width', 'opacity'], {
    duration: theme.transitions.duration.shorter,
  }),
  ...(isCollapse && {
    width: 0,
    opacity: 0,
  }),
}));

export const ListItemIconStyle = styled(ListItemIcon)(({ theme }) => ({
  width: theme.config.icon.size,
  height: theme.config.icon.size,
  minWidth: 'auto',
  marginRight: theme.spacing(2),
  display: 'flex',
  alignItems: 'center',
  justifyContent: 'center',
  '& svg': { width: '100%', height: '100%' },
}));
