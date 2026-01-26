export type TConfigRoot = {
  [key: string]: unknown;
};

export interface IConfigStore {
  read(): Promise<TConfigRoot>;
  write(config: TConfigRoot): void;
}
