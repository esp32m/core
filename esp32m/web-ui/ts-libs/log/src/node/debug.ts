import debug from 'debug';
import { getLogger } from '../impl';
import {
  IAppender,
  ILogger,
  LevelNames,
  LogLevel,
  TLogAppenderPlugin,
} from '../types';
import { pluginLog } from './plugin';

const Name = 'log-appender-debug';

export const interceptDebugLogOutput = (level = LogLevel.Debug) => {
  debug.log = (message) => {
    if (!message) return;
    const [, origin, ...parts] = message.split(' ');
    if (!origin || !parts || !parts.length) return;
    getLogger(origin).log(level, parts.join(' '));
  };
};

class DebugAppender implements IAppender {
  readonly name = Name;
  constructor() { }
  append(logger: ILogger, level: LogLevel, ...args: any[]): void {
    const [message, ...params] = args;
    debug(`${logger.name}:${LevelNames[level]}`)(message, params);
  }
}

export const pluginLogDebug = (): TLogAppenderPlugin => ({
  name: Name,
  log: {
    appender: () => new DebugAppender(),
  },
  use: [pluginLog]
});
