import { TPlugin } from '@ts-libs/plugins';
import { TSelector } from '@ts-libs/redux';
import { ComponentType, PropsWithChildren, ReactElement } from 'react';
import { RouteObject } from 'react-router-dom';

export type TUiRootPlugin = TPlugin & {
  readonly ui: {
    readonly root: {
      readonly hoc?: ComponentType<PropsWithChildren>;
      readonly leaf?: ComponentType;
    };
  };
};

export type TUiLayoutPlugin = TPlugin & {
  readonly ui: {
    readonly layout: ComponentType;
  };
};

export type TUiThemePlugin = TPlugin & {
  readonly ui: {
    readonly theme: Record<string, unknown>;
  };
};

export type TUiElementsPlugin = TPlugin & {
  readonly ui: {
    readonly header?: {
      readonly actions?: Array<ComponentType>;
    };
  };
};

export type TRoutesPlugin = TPlugin & {
  routes: Array<RouteObject>;
};

export interface TContent {
  title?: string;
  icon?: ComponentType;
  component?: ComponentType;
  auth?: boolean;
  routeChildren?: Array<RouteObject>;
  menu?: {
    section?: string;
    parent?: string;
    index?: number;
  };
}

export type TContentPlugin = TPlugin & {
  content: TContent;
};

export type TMenuItems = Array<TMenuItem>;
export type TMenuItem = {
  title: string;
  path: string;
  icon?: ReactElement;
  children?: TMenuItems;
};

export type TMainMenuPlugin = TPlugin & {
  mainMenu: {
    items: TMenuItems;
  };
};

export type TAppLoadingPlugin = TPlugin & {
  appLoadingSelector: TSelector<boolean>;
};
