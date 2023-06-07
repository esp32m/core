import winston, { format, LeveledLogMethod, Logger } from 'winston';
import { ILogger, ILoggerImpl, LogLevel, TLoggerPlugin } from '../types';

class WinstonLogger implements ILoggerImpl {
  readonly logger: ILogger;
  readonly _winston: Logger;
  constructor(logger: ILogger, transports: winston.transport[]) {
    this.logger = logger;
    const { name } = logger;
    const { combine, label, timestamp, printf } = format;

    const myFormat = printf(({ level, message, label, timestamp }) => {
      return `${timestamp} ${level} ${label}  ${message}`;
    });
    this._winston = winston.loggers.get(name, {
      format: combine(timestamp(), label({ label: name }), myFormat),
      transports,
    });
  }
  log(level: LogLevel, ...args: any[]): void {
    let lm: LeveledLogMethod = this._winston.info;
    switch (level) {
      case LogLevel.Error:
        lm = this._winston.error;
        break;
      case LogLevel.Warn:
        lm = this._winston.warn;
        break;
      case LogLevel.Info:
        lm = this._winston.info;
        break;
      case LogLevel.Debug:
        lm = this._winston.debug;
        break;
      case LogLevel.Verbose:
        lm = this._winston.verbose;
        break;
    }
    (lm as (...args: any[]) => void)(...args);
  }
}

export const pluginLogWinston = (
  ...transports: winston.transport[]
): TLoggerPlugin => ({
  name: 'logger-winston',
  logger: {
    impl: (logger) => {
      return Promise.resolve(new WinstonLogger(logger, transports));
    },
  },
});
