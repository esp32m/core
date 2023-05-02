export interface IProps {
  name: string;
  title?: string;
}

export const enum RelayState {
  Unknow = 'unknown',
  On = 'on',
  Off = 'off',
}

export interface IState {
  state: RelayState;
}

export type NameOrList = string | Array<[string, string]> | Array<string>;

export interface IMultiProps {
  nameOrList: NameOrList;
  title?: string;
}
