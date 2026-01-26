import winston, { format, LeveledLogMethod } from 'winston';
import { IAppender, ILogger, LogLevel, TLogAppenderPlugin } from '../types';
import { pluginLog } from './plugin';

const Name = 'log-appender-winston';

class WinstonAppender implements IAppender {
  readonly name = Name;
  constructor(readonly transports: winston.transport[]) {
  }
  append(logger: ILogger, level: LogLevel, ...args: any[]): void {
    const { name } = logger;
    const { combine, label, timestamp, printf } = format;

    const myFormat = printf(({ level, message, label, timestamp }) => {
      return `${timestamp} ${level} ${label}  ${message}`;
    });
    const w = winston.loggers.get(name, {
      format: combine(timestamp(), label({ label: name }), myFormat),
      transports: this.transports,
    });

    let lm: LeveledLogMethod = w.info;
    switch (level) {
      case LogLevel.Error:
        lm = w.error;
        break;
      case LogLevel.Warn:
        lm = w.warn;
        break;
      case LogLevel.Info:
        lm = w.info;
        break;
      case LogLevel.Debug:
        lm = w.debug;
        break;
      case LogLevel.Verbose:
        lm = w.verbose;
        break;
    }
    (lm as (...args: any[]) => void)(...args);
  }
}

export const pluginLogWinston = (
  ...transports: winston.transport[]
): TLogAppenderPlugin => ({
  name: Name,
  log: {
    appender: () => new WinstonAppender(transports),
  },
  use: [pluginLog]
});
