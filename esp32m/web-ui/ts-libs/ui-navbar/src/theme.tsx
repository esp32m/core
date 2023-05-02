import { alpha } from '@mui/material';
import { VerticalMenuHeader } from './vertical';
import { ComponentType, ReactElement } from 'react';
import { Widgets } from '@mui/icons-material';

const createCustomShadows = (color: string) => {
  const transparent = alpha(color, 0.16);
  return {
    z1: `0 1px 2px 0 ${transparent}`,
    z8: `0 8px 16px 0 ${transparent}`,
    z12: `0 12px 24px -4px ${transparent}`,
    z16: `0 16px 32px -4px ${transparent}`,
    z20: `0 20px 40px -4px ${transparent}`,
    z24: `0 24px 48px 0 ${transparent}`,
    dropdown: `0 0 2px 0 ${alpha(color, 0.24)}, -20px 20px 40px -4px ${alpha(
      color,
      0.24
    )}`,
  };
};

const config = {
  header: {
    mobileHeight: 64,
    desktopHeight: 92 - 16,
    desktopOffsetHeight: 92 - 32,
    content: (<></>) as ReactElement,
  },
  footer: {
    content: undefined as ComponentType | string | undefined,
  },
  navbar: {
    width: 280,
    collapseWidth: 88,
    itemRootHeight: 48,
    itemSubHeight: 40,
    itemHorizontalHeight: 32,
    header: (
      <VerticalMenuHeader icon={<Widgets />}>Main menu</VerticalMenuHeader>
    ) as ReactElement,
  },
  icon: {
    size: 22,
    sizeHorizontal: 20,
  },
};

interface CustomShadowOptions {
  z1: string;
  z8: string;
  z12: string;
  z16: string;
  z20: string;
  z24: string;
  dropdown: string;
}

declare module '@mui/material/styles' {
  interface Theme {
    customShadows: CustomShadowOptions;
    config: typeof config;
  }
  interface ThemeOptions {
    customShadows?: CustomShadowOptions;
  }
}

export const theme = {
  config,
  customShadows: createCustomShadows('#919EAB'),
};
