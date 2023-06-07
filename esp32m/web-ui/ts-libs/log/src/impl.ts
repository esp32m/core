import { getPlugins } from '@ts-libs/plugins';
import { ConsoleLogger } from './console';
import {
  ILogger,
  ILoggerImpl,
  LogLevel,
  TLoggerOptions,
  TLoggerPlugin,
} from './types';

const DefaultLevel = LogLevel.Info;

const implConstructor = (logger: ILogger) => {
  const plugins = getPlugins<TLoggerPlugin>((p) => !!p.logger?.impl);
  let result: Promise<ILoggerImpl> | undefined;
  for (const {
    logger: { impl },
  } of plugins) {
    result = impl(logger);
    if (result) break;
  }
  return result || Promise.resolve(new ConsoleLogger(logger));
};

class Logger implements ILogger {
  private _impl: Promise<ILoggerImpl> | undefined;
  get level() {
    return this._level || this.parent?.level || DefaultLevel;
  }
  set level(v: LogLevel) {
    this._level = v;
  }
  constructor(
    readonly name: string,
    readonly parent: ILogger | undefined,
    private _level: LogLevel | undefined
  ) {}
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
    if (level <= this.level)
      this.impl()
        .then((i) => {
          try {
            i.log(level, ...args);
          } catch (e) {
            try {
              if (console) {
                console.error(`${e} when logging a message:`);
                console.error(...args);
              }
            } catch {}
          }
        })
        .catch((e) => {
          try {
            if (console) {
              console.error(`${e} when creating a logger:`);
              console.error(...args);
            }
          } catch {}
        });
  }

  private impl() {
    return this._impl || (this._impl = implConstructor(this));
  }
}

const loggers: Record<string, Logger> = {};

export function getLogger(name: string, options?: TLoggerOptions): ILogger {
  const { parent } = options || {};
  let n = name;
  let p: ILogger | undefined;
  if (parent) {
    if (typeof parent === 'string') p = loggers[parent];
    else if (parent instanceof Logger) p = parent;
  }
  if (p) n = `${p.name}.${name}`;
  return loggers[n] || (loggers[n] = new Logger(n, p, options?.level));
}
