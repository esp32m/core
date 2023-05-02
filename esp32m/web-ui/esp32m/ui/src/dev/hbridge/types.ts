export interface IProps {
  name: string;
  title?: string;
}

export interface IMultiProps {
  nameOrList: string | Array<[string, string]>;
  title?: string;
}

export const enum Mode {
  Off,
  Forward,
  Reverse,
  Break,
}

export interface IState {
  mode: Mode;
  speed: number;
}
