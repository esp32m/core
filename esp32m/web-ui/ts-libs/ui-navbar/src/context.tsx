import { noop } from '@ts-libs/tools';
import {
  PropsWithChildren,
  createContext,
  useContext,
  useEffect,
  useState,
} from 'react';

import { useResponsive } from './utils';

export type CollapseDrawerContextProps = {
  isCollapse?: boolean;
  collapseClick: boolean;
  collapseHover: boolean;
  onToggleCollapse: VoidFunction;
  onHoverEnter: VoidFunction;
  onHoverLeave: VoidFunction;
};

const initialState: CollapseDrawerContextProps = {
  collapseClick: false,
  collapseHover: false,
  onToggleCollapse: noop,
  onHoverEnter: noop,
  onHoverLeave: noop,
};

const NavbarContext = createContext(initialState);

export function ContextProvider({ children }: PropsWithChildren) {
  const isDesktop = useResponsive('up', 'lg');

  const [collapse, setCollapse] = useState({
    click: false,
    hover: false,
  });

  useEffect(() => {
    if (!isDesktop) {
      setCollapse({
        click: false,
        hover: false,
      });
    }
  }, [isDesktop]);

  const handleToggleCollapse = () => {
    setCollapse({ ...collapse, click: !collapse.click });
  };

  const handleHoverEnter = () => {
    if (collapse.click) {
      setCollapse({ ...collapse, hover: true });
    }
  };

  const handleHoverLeave = () => {
    setCollapse({ ...collapse, hover: false });
  };

  return (
    <NavbarContext.Provider
      value={{
        isCollapse: collapse.click && !collapse.hover,
        collapseClick: collapse.click,
        collapseHover: collapse.hover,
        onToggleCollapse: handleToggleCollapse,
        onHoverEnter: handleHoverEnter,
        onHoverLeave: handleHoverLeave,
      }}
    >
      {children}
    </NavbarContext.Provider>
  );
}

export const useNavbarContext = () => useContext(NavbarContext);
