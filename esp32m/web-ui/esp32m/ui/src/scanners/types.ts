import React from 'react';
import { TPlugin } from '@ts-libs/plugins';

export type IScannerPlugin = TPlugin & {
  scanner: {
    component: React.ComponentType;
    props?: React.Attributes;
  };
}
