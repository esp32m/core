import { getPlugins, pluginsObservable, TPlugin } from '@ts-libs/plugins';
import { ConsoleAppender } from './console';
import {
  IAppender,
  ILogger,
  LevelNames,
  LogLevel,
  TLogAppenderPlugin,
  TLoggerOptions,
} from './types';
import { Subscription } from 'rxjs';

let defaultLevel = LogLevel.Info;

const pendingAppenders: Record<string, TLogAppenderPlugin> = {};
const appenders: Record<string, IAppender> = {};
let appendersSubscription: Subscription;
let defaultAppender: IAppender | undefined;

const getAppenders = () => {
  if (!appendersSubscription) {
    appendersSubscription =
      pluginsObservable.subscribe((p: TPlugin & Partial<TLogAppenderPlugin>) => {
        if (p.log?.appender) {
          pendingAppenders[p.name] = p as TLogAppenderPlugin;
        }
      });
    getPlugins<TLogAppenderPlugin>((p) => !!p.log?.appender).reduce((acc, p) => {
      acc[p.name] = p;
      return acc;
    }, pendingAppenders);
  }
  if (Object.keys(pendingAppenders).length) {
    for (const name in pendingAppenders) {
      const plugin = pendingAppenders[name];
      delete pendingAppenders[name];
      if (!appenders[plugin.name]) try {
        appenders[plugin.name] = plugin.log.appender();
      } catch (e) {
        try {
          if (console)
            console.error(`${e} when creating an appender ${plugin.name}`);
        } catch { }
      }
    }
  }
  if (!Object.keys(appenders).length) {
    if (!defaultAppender)
      defaultAppender = new ConsoleAppender();
    return [defaultAppender];
  }
  return Object.values(appenders);
}

class Logger implements ILogger {
  get level() {
    return this._level || this.parent?.level || defaultLevel;
  }
  set level(v: LogLevel) {
    this._level = v;
  }
  constructor(
    readonly name: string,
    readonly parent: ILogger | undefined,
    private _level: LogLevel | undefined,
    readonly group?: string
  ) { }
  try<T extends (...args: any) => any>(
    func: T
  ): (...args: any) => any | undefined {
    return (...args) => {
      try {
        return func(...args);
      } catch (e) {
        this.warn(e);
      }
    };
  }
  asyncTry<T extends (...args: any) => any>(
    func: T
  ): (...args: any) => any | undefined {
    return async (...args) => {
      try {
        return await func(...args);
      } catch (e) {
        this.warn(e);
      }
    };
  }
  dump(level: LogLevel, data: Uint8Array, ...args: Array<any>): void {
    if (level > this.level) return;
    if (args) this.log(level, args);
    const offsetPad = data.length < 0x10000 ? 4 : 8;
    for (let offset = 0; offset < data.length; offset += 16) {
      const lineBytes = Math.min(16, data.length - offset);
      let hexs = '';
      let asciis = '';
      for (let i = 0; i < lineBytes; i++) {
        const b = data[offset + i];
        hexs += `${b.toString(16).padStart(2, '0')} `;
        asciis += b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : '.';
      }
      hexs += '   '.repeat(16 - lineBytes);
      const line = `${offset
        .toString(16)
        .padStart(offsetPad, '0')} | ${hexs}| ${asciis}`;
      this.log(level, line);
    }
  }

  error(...args: Array<any>) {
    this.log(LogLevel.Error, ...args);
  }
  warn(...args: Array<any>) {
    this.log(LogLevel.Warn, ...args);
  }
  info(...args: Array<any>) {
    this.log(LogLevel.Info, ...args);
  }
  debug(...args: Array<any>) {
    this.log(LogLevel.Debug, ...args);
  }
  verbose(...args: Array<any>) {
    this.log(LogLevel.Verbose, ...args);
  }

  log(level: LogLevel, ...args: Array<any>) {
    if (level > this.level)
      return;
    const appenders = getAppenders();
    for (const appender of appenders) {
      try {
        appender.append(this, level, ...args);
      } catch (e) {
        try {
          if (console) {
            console.error(`${e} when logging a message:`);
            console.error(...args);
          }
        } catch { }
      }
    }
  }
}


const loggers: Record<string, Logger> = {};

export function getLogger(name: string, options?: TLoggerOptions): ILogger {
  const { parent, level, group, instance } = options || {};
  let n = name;
  let p: ILogger | undefined;
  if (parent) {
    if (typeof parent === 'string') p = loggers[parent];
    else if (parent instanceof Logger) p = parent;
  }
  if (p) n = `${p.name}.${name}`;
  if (instance) n += `[${instance}]`;
  return loggers[n] || (loggers[n] = new Logger(n, p, level, group));
}

export function setLogLevel(level: LogLevel | string) {
  let l: LogLevel = typeof level === 'string' ? LevelNames.indexOf(level) : level;
  if (l <= LogLevel.None) l = LogLevel.Error;
  if (l > LogLevel.Verbose) l = LogLevel.Verbose;
  defaultLevel = l;
}
