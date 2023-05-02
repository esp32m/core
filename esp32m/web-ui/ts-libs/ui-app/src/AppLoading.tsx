import { CircularProgress, styled } from '@mui/material';
import { getPlugins } from '@ts-libs/plugins';
import { TStateRoot, getRedux } from '@ts-libs/redux';
import { TAppLoadingPlugin } from '@ts-libs/ui-base';
import { useState } from 'react';
import { useSelector } from 'react-redux';

const Center = styled('div')({
  position: 'absolute',
  left: '50%',
  top: '50%',
  transform: 'translate(-50%, -50%)',
});

export const AppLoading = ({ children }: React.PropsWithChildren<unknown>) => {
  const plugins = getPlugins<TAppLoadingPlugin>();
  const { persistor } = getRedux();
  const [bootstrapped, setBootstrapped] = useState(!persistor);
  persistor?.then(() => setBootstrapped(true));
  const loading = useSelector<TStateRoot>((state) => {
    let result = false;
    for (const p of plugins)
      if (p.appLoadingSelector && p.appLoadingSelector(state)) {
        result = true;
        break;
      }
    return result;
  });
  if (!bootstrapped || loading) {
    return (
      <Center>
        <CircularProgress />
      </Center>
    );
  }
  return <>{children}</>;
};
