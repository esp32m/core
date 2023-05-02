import { ILogger, ILoggerImpl, LogLevel } from './types';

export class ConsoleLogger implements ILoggerImpl {
  readonly logger: ILogger;
  constructor(logger: ILogger) {
    this.logger = logger;
  }
  log(level: LogLevel, ...args: any[]): void {
    const [message, ...params] = args;
    const text = `${this.logger.name}  ${message}`;
    switch (level) {
      case LogLevel.Error:
        console.error(text, ...params);
        break;
      case LogLevel.Warn:
        console.warn(text, ...params);
        break;
      case LogLevel.Info:
        console.info(text, ...params);
        break;
      case LogLevel.Debug:
        console.debug(text, ...params);
        break;
      case LogLevel.Verbose:
        console.trace(text, ...params);
        break;
    }
  }
}
