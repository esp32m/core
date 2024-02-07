import { ComponentType, useMemo } from 'react';
import { useLocation, useMatches } from 'react-router-dom';

export type TNavPathHandle = {
  readonly populateNavPath: (p: NavPath) => void;
};

export type TNavPathSegment = {
  readonly name: string;
  readonly path: string;
  readonly segments: string[];
  label?: string | ComponentType;
};

export class NavPath {
  readonly segments: Array<TNavPathSegment> = [];
  constructor(readonly path: string) {
    this.path
      .split('/')
      .filter((s) => !!s)
      .reduce(
        (segments, name) => (
          segments.push(name),
          this.segments.push({ name, path: segments.join('/'), segments }),
          segments
        ),
        [] as string[]
      );
  }
}

export function useNavPath() {
  const { pathname } = useLocation();
  const matches = useMatches();
  return useMemo(() => {
    const np = new NavPath(pathname);
    for (const m of matches) {
      const { populateNavPath } = (m?.handle as TNavPathHandle) || {};
      populateNavPath?.(np);
    }
    return np;
  }, [pathname, matches]);
}
