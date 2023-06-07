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
  ILogger,
  ILoggerImpl,
  LevelNames,
  LogLevel,
  TLoggerPlugin,
} from '../types';

type TOptions = {
  readonly roll?: number;
};

class File {
  private _stream: WriteStream | undefined;
  constructor(readonly path: string, readonly rollCount: number) {}
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
const files: Record<string, File> = {};

class FileLogger implements ILoggerImpl {
  private _file: File;
  constructor(
    readonly logger: ILogger,
    readonly path: string,
    readonly options?: TOptions
  ) {
    const p = resolve(normalize(path));
    this._file = files[p] || (files[p] = new File(p, options?.roll || 0));
  }
  log(level: LogLevel, ...args: any[]): void {
    const [message, ...params] = args.map((a) => {
      if (a instanceof Error && a.stack) return a.stack;
      return a;
    });
    const text = `${new Date().toISOString()} ${LevelNames[level]} ${
      this.logger.name
    }  ${message}${params.length > 0 ? ' ' + JSON.stringify(params) : ''}`;
    this._file.writeLine(text);
  }
}

export const pluginLogFile = (
  path: string,
  options?: TOptions
): TLoggerPlugin => ({
  name: 'logger-file',
  logger: {
    impl: (logger) => {
      return Promise.resolve(new FileLogger(logger, path, options));
    },
  },
});
