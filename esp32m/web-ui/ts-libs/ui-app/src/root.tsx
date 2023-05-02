import { createElement, ReactElement, Suspense, useMemo } from 'react';
import { RouterProvider, createBrowserRouter } from 'react-router-dom';
import { TUiLayoutPlugin, TUiRootPlugin } from '@ts-libs/ui-base';
import { getPlugins } from '@ts-libs/plugins';
import { TContentPlugin, TRoutesPlugin } from '@ts-libs/ui-base';
import { Navigate, RouteObject } from 'react-router-dom';
import { AppLoading } from './AppLoading';
import { ThemeRoot } from './ThemeRoot';
import { Provider } from 'react-redux';
import { getRedux } from '@ts-libs/redux';
import { NotFound } from './404';

export function useRouteObjects() {
  const plugins = getPlugins<TRoutesPlugin & TContentPlugin>();
  return useMemo(() => {
    let hasHome = false;
    const routes: Array<RouteObject> = [];
    plugins.forEach((p) => {
      if (p.routes) routes.push(...p.routes);
      if (p.content && p.content.component) {
        const element = createElement(p.content.component);
        // TODO: rework to a plugin
        // if (p.content.auth) element = createElement(RequireAuth, {}, element);
        if (p.name == 'home') hasHome = true;
        routes.push({
          path: p.name,
          element,
          children: p.content.routeChildren,
        });
      }
    });
    if (hasHome)
      routes.push({
        index: true,
        element: createElement(Navigate, { to: '/home' }),
      });
    return routes;
  }, [plugins]);
}

function useLayout() {
  const plugins = getPlugins<TUiLayoutPlugin>();
  const layouts = useMemo(
    () => plugins.filter((p) => !!p.ui?.layout),
    [plugins]
  );
  if (layouts.length != 1)
    throw new Error(
      'expected exactly one root layout to be defined, got ' + layouts.length
    );
  return layouts[0].ui.layout;
}

function useRouter() {
  const routeObjects = useRouteObjects();
  const Layout = useLayout();
  return useMemo(
    () =>
      createBrowserRouter([
        {
          path: '/',
          element: <Layout />,
          children: [...routeObjects, { path: '*', element: <NotFound /> }],
        },
      ]),
    [routeObjects, Layout]
  );
}

export function Root() {
  const router = useRouter();
  const plugins = getPlugins<TUiRootPlugin>();
  const tree = useMemo(() => {
    let children: Array<ReactElement> = [
      <AppLoading key={0}>
        <RouterProvider router={router} />
      </AppLoading>,
    ];
    for (const {
      ui: {
        root: { hoc, leaf },
      },
    } of plugins.filter((p) => !!p.ui?.root).reverse()) {
      if (leaf)
        children = [...children, createElement(leaf, { key: children.length })];
      if (hoc) children = [createElement(hoc, { key: 0 }, children)];
    }
    return children;
  }, [plugins, router]);

  const { store } = getRedux();
  return (
    <ThemeRoot>
      <Suspense fallback={<h1>🌀 Loading...</h1>}>
        <Provider store={store}>{tree}</Provider>
      </Suspense>
    </ThemeRoot>
  );
}
