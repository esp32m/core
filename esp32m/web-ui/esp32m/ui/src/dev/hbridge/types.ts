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
  Brake,
}

export type TState = {
  mode: Mode;
};

export type TConfig = {
  speed: number;
};
