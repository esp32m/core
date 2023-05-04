import { useEffect } from 'react';
import { useSelector } from 'react-redux';
import { Name, selectors } from './state';
import { useModuleState } from '../../backend';
import { TOtaState } from './types';
import {
  Dialog,
  DialogTitle,
  Box,
  LinearProgress,
  Typography,
  styled,
} from '@mui/material';
import { useTranslation } from '@ts-libs/ui-i18n';

const ProgressDialog = styled(Dialog)({
  flexGrow: 1,
});
const Percent = styled(Typography)({
  textAlign: 'right',
});
const ProgressBox = styled(Box)({
  margin: 24,
});
const BorderLinearProgress = styled(LinearProgress)(({ theme }) => ({
  '& .MuiLinearProgress-root': {
    height: 10,
    borderRadius: 5,
  },
  '& .MuiLinearProgress-colorPrimary': {
    backgroundColor:
      theme.palette.grey[theme.palette.mode === 'light' ? 200 : 700],
  },
  '& .MuiLinearProgress-bar': {
    borderRadius: 5,
    backgroundColor: '#1a90ff',
  },
}));

export const Hoc = ({ children }: React.PropsWithChildren<unknown>) => {
  const { isRunning, isFinished, stateLoaded } = useSelector(selectors.state);
  const state = useModuleState<TOtaState>(Name, {
    condition: isRunning || !stateLoaded,
  });
  const { t } = useTranslation();
  useEffect(() => {
    if (isFinished) setTimeout(() => window.location.reload(), 3000); // wait at least 3 sec for the chip to boot and reconnect
  }, [isFinished]);
  const percent =
    state && state.total ? (state.progress * 100) / state.total : 0;
  return (
    <>
      {children}
      <ProgressDialog open={isRunning}>
        <DialogTitle>{t('Firmware update is in progress')}</DialogTitle>
        <ProgressBox display="flex" alignItems="center">
          <Box width="100%" mr={1}>
            <BorderLinearProgress variant="determinate" value={percent} />
          </Box>
          <Box minWidth={35}>
            <Percent variant="body2" color="textSecondary">{`${Math.round(
              percent
            )}%`}</Percent>
          </Box>
        </ProgressBox>
      </ProgressDialog>
    </>
  );
};
