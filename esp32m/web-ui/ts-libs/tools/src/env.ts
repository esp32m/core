export const isBrowser = () =>
  typeof window !== 'undefined' && typeof window.document !== 'undefined';
export const isNode = () =>
  !!(typeof process !== 'undefined' && process.version);

export const enum Destination {
  Local = 'local',
  Remote = 'remote',
  Analyze = 'analyze',
}

export const enum Mode {
  Development = 'development',
  Production = 'production',
}

type TBuildInfo = {
  version: string;
  built: string;
  mode: Mode;
  destination: Destination;
};

declare global {
  interface Window {
    __build_info: TBuildInfo | undefined;
  }
  // eslint-disable-next-line no-var
  var __build_info: TBuildInfo | undefined;
}

export const buildInfo = isBrowser()
  ? window.__build_info
  : global.__build_info;

export const isDevelopment = () => buildInfo?.mode == Mode.Development;
export const isProduction = () => buildInfo?.mode == Mode.Production;

export const isDestinationLocal = () =>
  buildInfo?.destination == Destination.Local;
export const isDestinationRemote = () =>
  buildInfo?.destination == Destination.Remote;
export const isDestinationAnalyze = () =>
  buildInfo?.destination == Destination.Analyze;
