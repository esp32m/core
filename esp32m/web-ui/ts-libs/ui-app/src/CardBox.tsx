import React from 'react';

import {
  Card,
  CardHeader,
  CardContent,
  Divider,
  Avatar,
  LinearProgress,
  Grid,
  CardProps,
} from '@mui/material';
import { styled } from '@mui/material/styles';
import { useTranslation } from '@ts-libs/ui-i18n';

const StyledCard = styled(Card)(({ theme }) => ({
  /*[theme.breakpoints.down('sm')]: {
    width: '100%',
  },*/
}));

const StyledGridItem = styled(Grid)(({ theme }) => ({
  width: 560,
  minWidth: 300,
  maxWidth: 640,
  margin: 20,
  [theme.breakpoints.down('sm')]: {
    width: '100%',
  },
}));
const StyledCardHeader = styled(CardHeader)({
  '& .MuiCardHeader-action': {
    margin: 0,
  },
});

const StyledDivider = styled(Divider)({
  marginTop: 4,
});

interface IProps extends CardProps {
  title?: any;
  header?: React.ReactElement;
  avatar?: React.ReactNode;
  action?: React.ReactNode;
  progress?: boolean | number;
}

export function CardBox({
  title,
  header,
  children,
  avatar,
  action,
  progress,
  ...cardProps
}: React.PropsWithChildren<IProps>) {
  const { t } = useTranslation();
  let pc;
  switch (typeof progress) {
    case 'boolean':
      if (progress) pc = <LinearProgress />;
      break;
    case 'number':
      pc = <LinearProgress variant="determinate" value={progress} />;
      break;
  }
  if (!pc) pc = <StyledDivider />;
  let h = header;
  if (!h && title) {
    h = (
      <>
        <StyledCardHeader
          avatar={avatar || <Avatar>{title[0]}</Avatar>}
          title={t(title)}
          slotProps={{title: { variant: 'h5' }}}
          action={action}
        />
        {pc}
      </>
    );
  }
  return (
    <StyledGridItem>
      <StyledCard {...cardProps}>
        {h}
        <CardContent>{children}</CardContent>
        <div />
      </StyledCard>
    </StyledGridItem>
  );
}
