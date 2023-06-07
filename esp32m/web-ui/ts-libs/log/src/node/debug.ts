import debug from 'debug';
import { getLogger } from '../impl';
import {
  ILogger,
  ILoggerImpl,
  LevelNames,
  LogLevel,
  TLoggerPlugin,
} from '../types';

export const interceptDebugLogOutput = (level = LogLevel.Debug) => {
  debug.log = (message) => {
    if (!message) return;
    const [, origin, ...parts] = message.split(' ');
    if (!origin || !parts || !parts.length) return;
    getLogger(origin).log(level, parts.join(' '));
  };
};

class DebugLogger implements ILoggerImpl {
  constructor(readonly logger: ILogger) {}
  log(level: LogLevel, ...args: any[]): void {
    const [message, ...params] = args;
    debug(`${this.logger.name}:${LevelNames[level]}`)(message, params);
  }
}

export const pluginLogDebug = (): TLoggerPlugin => ({
  name: 'logger-debug',
  logger: {
    impl: (logger) => {
      return Promise.resolve(new DebugLogger(logger));
    },
  },
});
