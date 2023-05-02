import { PropsWithChildren, ReactElement } from 'react';
import { useNavbarContext } from '../context';
import { Box, Typography } from '@mui/material';

type TProps = {
  icon?: ReactElement;
};

export const VerticalMenuHeader = ({
  icon,
  children,
}: PropsWithChildren<TProps>) => {
  const { isCollapse } = useNavbarContext();
  return (
    <>
      {icon && <Box sx={{ width: 21, height: 24 }}>{icon}</Box>}
      {!isCollapse && children && (
        <Typography
          noWrap
          component="h1"
          variant="h6"
          color={'textSecondary'}
          sx={{ paddingLeft: 1 }}
        >
          {children}
        </Typography>
      )}
    </>
  );
};
