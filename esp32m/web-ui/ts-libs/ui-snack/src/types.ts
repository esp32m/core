import { AlertColor } from '@mui/material';
import { ReactNode } from 'react';

export type TSnackItem = {
  message: string;
  progress?: boolean;
  timer?: boolean;
  severity?: AlertColor;
  action?: ReactNode;
  title?: string;
  timeout?: number;
};

export interface ISnackItem {
  readonly id: number;
  close(): void;
}

export interface ISnackApi {
  add(item: TSnackItem): ISnackItem;
  error(e: Error | string): ISnackItem;
  suspendClosure(): void;
  resumeClosure(): void;
}
