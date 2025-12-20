import { useEffect } from 'react';
import { client } from './client';
import { ClientContext } from './hooks';

export const Hoc = ({ children }: React.PropsWithChildren<unknown>) => {
  const c = client();
  useEffect(() => {
    const init = async () => {
      await c.open();
      try {
        const info = await c.getInfo("app");
        const name = info?.data?.name;
        if (name) {
          document.title = name + ' - ' + document.title;
        }
      } catch { }
    };
    init();
    return () => {
      c.close();
    };
  }, [c]);
  return <ClientContext.Provider value={c}>{children}</ClientContext.Provider>;
};
