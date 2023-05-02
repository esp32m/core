import { TPlugin } from '@ts-libs/plugins';
import React from 'react';

export type IDebugPlugin = TPlugin & {
  debug: {
    content: React.ComponentType;
    props?: React.Attributes;
  };
}
