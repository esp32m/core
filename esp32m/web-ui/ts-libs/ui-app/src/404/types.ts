import { TPlugin } from '@ts-libs/plugins';
import { ComponentType } from 'react';

export interface I404Plugin extends TPlugin {
  component404: ComponentType;
}
