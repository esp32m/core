import { getPlugins } from '@ts-libs/plugins';
import { useMemo } from 'react';
import { Default } from './default';
import { I404Plugin } from './types';
import React from 'react';

export function use404Elements() {
  const plugins = getPlugins<I404Plugin>();
  return useMemo(() => {
    const elements = plugins.filter((p) => !!p.component404).map(({ component404 }) =>
      React.createElement(component404)
    );
    if (!elements.length) elements.push(React.createElement(Default));
    return elements;
  }, [plugins]);
}

export function use404Element() {
  const elements = use404Elements();
  return elements[Math.floor(Math.random() * elements.length)];
}
