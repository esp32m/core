import { useEffect } from 'react';
import { client } from './client';
import { ClientContext } from './hooks';

export const Hoc = ({ children }: React.PropsWithChildren<unknown>) => {
  const c = client();
  useEffect(() => {
    c.open();
    return () => {
      c.close();
    };
  }, [c]);
  return <ClientContext.Provider value={c}>{children}</ClientContext.Provider>;
};
