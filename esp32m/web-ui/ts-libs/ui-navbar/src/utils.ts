import { matchPath } from 'react-router-dom';
import { useState, useEffect, useMemo, createElement } from 'react';
import { Breakpoint } from '@mui/material';
import { useTheme, Theme, alpha } from '@mui/material/styles';
import { useMediaQuery } from '@mui/material';
import {
  TContentPlugin,
  TMainMenuPlugin,
  TUiElementsPlugin,
} from '@ts-libs/ui-base';
import { INavItem, NavSectionConfig } from './types';
import { getPlugins } from '@ts-libs/plugins';
import { useTranslation } from '@ts-libs/ui-i18n';

type Query = 'up' | 'down' | 'between' | 'only';
type Key = Breakpoint | number;
type Start = Breakpoint | number;
type End = Breakpoint | number;

export function useResponsive(
  query: Query,
  key?: Key,
  start?: Start,
  end?: End
) {
  const { breakpoints } = useTheme();

  const mediaUp = useMediaQuery(breakpoints.up(key as Key));
  const mediaDown = useMediaQuery(breakpoints.down(key as Key));
  const mediaBetween = useMediaQuery(
    breakpoints.between(start as Start, end as End)
  );
  const mediaOnly = useMediaQuery(breakpoints.only(key as Breakpoint));

  if (query === 'up') {
    return mediaUp;
  }

  if (query === 'down') {
    return mediaDown;
  }

  if (query === 'between') {
    return mediaBetween;
  }

  if (query === 'only') {
    return mediaOnly;
  }
}

export function useOffSetTop(top: number) {
  const [offsetTop, setOffSetTop] = useState(false);
  const isTop = top || 100;

  useEffect(() => {
    window.onscroll = () => {
      if (window.pageYOffset > isTop) {
        setOffSetTop(true);
      } else {
        setOffSetTop(false);
      }
    };
    return () => {
      window.onscroll = null;
    };
  }, [isTop]);

  return offsetTop;
}

type BackgroundBlurProps = {
  blur?: number;
  opacity?: number;
  color?: string;
};

type BackgroundGradientProps = {
  direction?: string;
  startColor?: string;
  endColor?: string;
};

interface BackgroundImageProps extends BackgroundGradientProps {
  url?: string;
}

function getDirection(value = 'bottom') {
  return {
    top: 'to top',
    right: 'to right',
    bottom: 'to bottom',
    left: 'to left',
  }[value];
}

export function cssStyles(theme?: Theme) {
  return {
    bgBlur: (props?: BackgroundBlurProps) => {
      const color =
        props?.color || theme?.palette.background.default || '#000000';

      const blur = props?.blur || 6;
      const opacity = props?.opacity || 0.8;

      return {
        backdropFilter: `blur(${blur}px)`,
        WebkitBackdropFilter: `blur(${blur}px)`, // Fix on Mobile
        backgroundColor: alpha(color, opacity),
      };
    },
    bgGradient: (props?: BackgroundGradientProps) => {
      const direction = getDirection(props?.direction);
      const startColor = props?.startColor || `${alpha('#000000', 0)} 0%`;
      const endColor = props?.endColor || '#000000 75%';

      return {
        background: `linear-gradient(${direction}, ${startColor}, ${endColor});`,
      };
    },
    bgImage: (props?: BackgroundImageProps) => {
      const url =
        props?.url ||
        'https://minimal-assets-api.vercel.app/assets/images/bg_gradient.jpg';
      const direction = getDirection(props?.direction);
      const startColor =
        props?.startColor || alpha(theme?.palette.grey[900] || '#000000', 0.88);
      const endColor =
        props?.endColor || alpha(theme?.palette.grey[900] || '#000000', 0.88);

      return {
        background: `linear-gradient(${direction}, ${startColor}, ${endColor}), url(${url})`,
        backgroundSize: 'cover',
        backgroundRepeat: 'no-repeat',
        backgroundPosition: 'center center',
      };
    },
  };
}

export const useHeaderActions = () => {
  const plugins = getPlugins<TUiElementsPlugin>();
  return useMemo(
    () =>
      plugins.flatMap(
        (p) =>
          p.ui?.header?.actions?.map((a, i) =>
            createElement(a, { key: `${p.name}-${i}` })
          ) || []
      ),
    [plugins]
  );
};

export const useMainMenuSections = () => {
  type Item = INavItem & { plugin: TContentPlugin };
  const plugins = getPlugins<TContentPlugin & TMainMenuPlugin>();
  const { t, i18n } = useTranslation();
  return useMemo(() => {
    function toNavItem(plugin: TContentPlugin) {
      const { name, content } = plugin;
      const { title, icon } = content;
      const item: Item = {
        path: '/' + name,
        title: t(title || name),
        icon: icon && createElement(icon),
        plugin,
      };
      return item;
    }
    function getIndex({ plugin: { name, content } }: Item) {
      const { menu } = content || {};
      return menu?.index != undefined ? menu.index : name == 'home' ? -1 : 0;
    }
    function sort(items?: Array<Item>) {
      return items?.sort((a, b) => getIndex(a) - getIndex(b));
    }
    const contentPlugins = plugins.filter(
      ({ content }) => !!(content?.title || content?.icon)
    );
    const byName = contentPlugins.reduce((m, c) => {
      m[c.name] = toNavItem(c);
      return m;
    }, {} as Record<string, Item>);
    const menuPlugins = plugins.filter(({ mainMenu }) => !!mainMenu?.items);
    menuPlugins.reduce((m, c) => {
      c.mainMenu.items.forEach(
        (item, i) => (m[c.name + i] = { plugin: c, ...item })
      );
      return m;
    }, byName);
    const bySection: Record<string, INavItem[]> = {};
    Object.values(byName).forEach((i) => {
      const {
        plugin: { content },
      } = i;
      const { menu } = content || {};
      const pi = menu?.parent && byName[menu.parent];
      if (pi) {
        const children = pi.children || (pi.children = []);
        children.push(i);
      } else {
        const sn = menu?.section || '';
        (bySection[sn] || (bySection[sn] = [])).push(i);
      }
    });
    Object.values(byName).forEach((i) => sort(i.children as Item[]));
    return Object.keys(bySection).map((sn) => {
      return {
        subheader: sn ? sn : undefined,
        items: sort(bySection[sn] as Item[]),
      } as NavSectionConfig;
    });
  }, [plugins, i18n.language]);
};

export function isExternalLink(path: string) {
  return path.startsWith('http');
}

export function getActive(path: string, pathname: string) {
  return path ? !!matchPath({ path: path, end: false }, pathname) : false;
}
