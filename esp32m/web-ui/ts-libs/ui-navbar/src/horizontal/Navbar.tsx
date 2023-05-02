import { Container } from '@mui/material';
import { styled } from '@mui/material/styles';
import { memo } from 'react';
import { TNavMenu } from '..';
import NavSectionHorizontal from './Section';

// ----------------------------------------------------------------------

const RootStyle = styled('div')(({ theme }) => ({
  transition: theme.transitions.create('top', {
    easing: theme.transitions.easing.easeInOut,
    duration: theme.transitions.duration.shorter,
  }),
  width: '100%',
  position: 'fixed',
  zIndex: theme.zIndex.appBar,
  padding: theme.spacing(1, 0),
  boxShadow: theme.customShadows.z8,
  top: theme.config.header.desktopOffsetHeight,
  backgroundColor: theme.palette.background.default,
}));

// ----------------------------------------------------------------------

type Props = {
  menu: TNavMenu;
};

function NavbarHorizontal({ menu }: Props) {
  return (
    <RootStyle>
      <Container maxWidth={false}>
        <NavSectionHorizontal navConfig={menu.sections} />
      </Container>
    </RootStyle>
  );
}

export default memo(NavbarHorizontal);
