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

type WrappedTry<T extends (...args: Parameters<T>) => ReturnType<T>> = (
  ...args: Parameters<T>
) => ReturnType<T> | undefined;

export interface ILogger {
  readonly name: string;
  readonly parent?: ILogger;
  level: LogLevel;
  readonly group?: string;
  readonly error: LevelLogMethod;
  readonly warn: LevelLogMethod;
  readonly info: LevelLogMethod;
  readonly debug: LevelLogMethod;
  readonly verbose: LevelLogMethod;
  log(level: LogLevel, ...args: Array<any>): void;
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

export interface IAppender {
  readonly name: string;
  append(logger: ILogger, level: LogLevel, ...args: Array<any>): void;
}

export type TAppenderConstructor = (
) => IAppender;

export type TLogAppenderPlugin = TPlugin & {
  log: {
    appender: TAppenderConstructor;
  };
};

export type TLoggerOptions = {
  parent?: ILogger | string;
  level?: LogLevel;
  group?: string;
  instance?: number;
};
