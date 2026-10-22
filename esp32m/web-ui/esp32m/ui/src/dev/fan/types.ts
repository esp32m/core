export type TState = {
  sampledAtMs: number;
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
