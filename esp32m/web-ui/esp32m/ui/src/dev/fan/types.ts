export type TState = {
  stamp: number;
  on: boolean;
  rpm?: number;
  speed?: number;
};

export type TConfig = {
  freq: number;
  speed: number;
  on: number;
};

export type TProps = {
  name: string;
  title?: string;
};
