import { TPlugin } from '@ts-libs/plugins';
import React from 'react';

export type TDebugPlugin = TPlugin & {
  debug: {
    content: React.ComponentType;
    props?: React.Attributes;
  };
};
