import { getPlugins } from '@ts-libs/plugins';
import { TStateRoot, getRedux } from '@ts-libs/redux';
import { Loading, TAppLoadingPlugin } from '@ts-libs/ui-base';
import { useState } from 'react';
import { useSelector } from 'react-redux';

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
  if (!bootstrapped || loading) return <Loading />;
  return <>{children}</>;
};
