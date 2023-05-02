import { useSelector } from 'react-redux';
import { selectors } from './state';
import {
  Alert,
  AlertTitle,
  Box,
  BoxProps,
  Collapse,
  LinearProgress,
  Snackbar,
  SnackbarContent,
  SnackbarProps,
  Stack,
} from '@mui/material';
import { TransitionGroup } from 'react-transition-group';
import { PropsWithChildren, ReactElement } from 'react';
import { useSnackApi } from './hooks';

export const MyStack = ({ children }: PropsWithChildren<{}>) => (
  <Stack spacing={2}>{children}</Stack>
);

export const Snacks = () => {
  const items = useSelector(selectors.items);
  const api = useSnackApi();
  const content = items.map(
    ({ message, severity, title, action, progress, closure, timer }, i) => {
      const children: ReactElement[] = [];
      // const timeLeft = closure && timer &&
      if (severity)
        children.push(
          <Alert severity={severity} action={action} key={0}>
            {title && <AlertTitle>{title}</AlertTitle>}
            {message}
          </Alert>
        );
      else
        children.push(
          <SnackbarContent
            message={message}
            title={title}
            action={action}
            elevation={3}
            square
            key={0}
          ></SnackbarContent>
        );
      if (closure) {
        if (progress) {
          children.push(
            <LinearProgress
              key={1}
              variant="determinate"
              value={closure.rel * 100}
            ></LinearProgress>
          );
        }
      }
      return <Collapse key={i}>{children}</Collapse>;
    }
  );
  const props: SnackbarProps = {
    open: items.length > 0,
    anchorOrigin: { vertical: 'bottom', horizontal: 'right' },
  };
  const boxProps: BoxProps = {
    sx: { width: '100%' },
    onMouseEnter: () => api.suspendClosure(),
    onMouseLeave: () => api.resumeClosure(),
  };
  return (
    <Snackbar {...props}>
      <Box {...boxProps}>
        <TransitionGroup component={MyStack}>{content}</TransitionGroup>
      </Box>
    </Snackbar>
  );
};
