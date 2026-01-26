import { IAppender, ILogger, LogLevel } from './types';

const Name='log-appender-console';

export class ConsoleAppender implements IAppender {
  readonly name = Name;
  append(logger: ILogger, level: LogLevel, ...args: any[]): void {
    const [message, ...params] = args;
    const text = `${logger.name}  ${message}`;
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
