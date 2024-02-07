import { TPlugin } from '@ts-libs/plugins';

type LevelLogMethod = (...args: Array<any>) => void;

export const enum LogLevel {
  None,
  Error,
  Warn,
  Info,
  Debug,
  Verbose,
}

export const LevelNames = ['', 'error', 'warn', 'info', 'debug', 'verbose'];

export interface ILoggerBase {
  log(level: LogLevel, ...args: Array<any>): void;
}

type WrappedTry<T extends (...args: Parameters<T>) => ReturnType<T>> = (
  ...args: Parameters<T>
) => ReturnType<T> | undefined;

export interface ILogger extends ILoggerBase {
  readonly name: string;
  readonly parent?: ILogger;
  level: LogLevel;
  readonly group?: string;
  readonly error: LevelLogMethod;
  readonly warn: LevelLogMethod;
  readonly info: LevelLogMethod;
  readonly debug: LevelLogMethod;
  readonly verbose: LevelLogMethod;
  dump(level: LogLevel, data: Uint8Array, ...args: Array<any>): void;
  try<T extends (...args: any) => any>(func: T): WrappedTry<T>;
  asyncTry<
    P extends any[],
    T extends (...args: P) => any,
    R extends ReturnType<T>,
  >(
    func: (...args: P) => R
  ): WrappedTry<T>;
}

export interface ILoggerImpl extends ILoggerBase {
  readonly logger: ILogger;
}

export type TLoggerImplConstructor = (
  logger: ILogger
) => Promise<ILoggerImpl> | undefined;

export type TLoggerPlugin = TPlugin & {
  logger: {
    impl: TLoggerImplConstructor;
  };
};

export type TLoggerOptions = {
  parent?: ILogger | string;
  level?: LogLevel;
  group?: string;
  instance?: number;
};
