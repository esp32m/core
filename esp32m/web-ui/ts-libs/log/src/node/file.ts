import {
  createWriteStream,
  existsSync,
  readdirSync,
  renameSync,
  unlinkSync,
  WriteStream,
} from 'fs';
import { EOL } from 'os';
import { basename, dirname, join, normalize, resolve } from 'path';
import {
  IAppender,
  ILogger,
  LevelNames,
  LogLevel,
  TLogAppenderPlugin,
} from '../types';
import { pluginLog } from './plugin';

const Name = 'log-appender-file';

type TOptions = {
  readonly roll?: number;
};

class File {
  private _stream: WriteStream | undefined;
  constructor(
    readonly path: string,
    readonly rollCount: number
  ) { }
  writeLine(line: string) {
    if (!this._stream) {
      const dir = dirname(this.path);
      const name = basename(this.path);
      const roll = (p: string, i: number) => {
        if (!existsSync(p)) return;
        if (i >= this.rollCount) unlinkSync(p);
        else renameSync(p, join(dir, name + '.' + (i + 1)));
      };
      readdirSync(dir)
        .reduce((a, f) => {
          const p = f.split(name + '.');
          let n: number;
          if (p.length == 2 && !p[0].length && !isNaN((n = parseInt(p[1]))))
            a.push(n);
          return a;
        }, [] as number[])
        .sort((a, b) => b - a)
        .forEach((n) => roll(join(dir, name + '.' + n), n));

      if (existsSync(this.path)) roll(this.path, 0);
      this._stream = createWriteStream(this.path, { flags: 'w' });
    }
    this._stream.write(line);
    this._stream.write(EOL);
  }
}

class FileAppender implements IAppender {
  readonly name = Name;
  private readonly files: Record<string, File> = {};
  constructor(
    readonly path: Promise<string>,
    readonly options?: TOptions
  ) {
  }
  append(logger: ILogger, level: LogLevel, ...args: any[]): void {
    this.path.then((path) => {
      const { group } = logger;
      if (group) {
        const dir = dirname(path);
        const name = basename(path);
        path = join(dir, `${group}-${name}`);
      }
      const p = resolve(normalize(path));
      const file = this.files[p] || (this.files[p] = new File(p, this.options?.roll || 0));

      const [message, ...params] = args.map((a) => {
        if (a instanceof Error && a.stack) return a.stack;
        return a;
      });
      const text = `${new Date().toISOString()} ${LevelNames[level]} ${logger.name
        }  ${message}${params.length > 0 ? ' ' + JSON.stringify(params) : ''}`;
      file.writeLine(text);
    }
    ).catch((e) => {
      try {
        if (console) {
          console.error(`${e} when logging a message with FileAppender:`);
          console.error(...args);
        }
      } catch { }
    });
  }
}

export const pluginLogFile = (
  path: string | Promise<string>,
  options?: TOptions
): TLogAppenderPlugin => ({
  name: Name,
  log: {
    appender: () => new FileAppender(Promise.resolve(path), options),
  },
  use: [pluginLog]
});
