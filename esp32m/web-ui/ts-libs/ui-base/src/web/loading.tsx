import { CircularProgress, styled } from '@mui/material';

const Center = styled('div')({
  position: 'absolute',
  left: '50%',
  top: '50%',
  transform: 'translate(-50%, -50%)',
});

export const Loading = () => {
  return (
    <Center>
      <CircularProgress />
    </Center>
  );
};
