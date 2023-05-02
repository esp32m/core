import { createWriteStream, WriteStream } from 'fs';
import { EOL } from 'os';
import { normalize, resolve } from 'path';
import {
  ILogger,
  ILoggerImpl,
  LevelNames,
  LogLevel,
  TLoggerPlugin,
} from '../types';

class File {
  private _stream: WriteStream | undefined;
  constructor(readonly path: string) {}
  writeLine(line: string) {
    if (!this._stream)
      this._stream = createWriteStream(this.path, { flags: 'w' });
    this._stream.write(line);
    this._stream.write(EOL);
  }
}
const files: Record<string, File> = {};

class FileLogger implements ILoggerImpl {
  private _file: File;
  constructor(readonly logger: ILogger, readonly path: string) {
    const p = resolve(normalize(path));
    this._file = files[p] || (files[p] = new File(p));
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

export const FileLogPlugin = (path: string): TLoggerPlugin => ({
  name: 'logger-file',
  logger: {
    impl: (logger) => {
      return Promise.resolve(new FileLogger(logger, path));
    },
  },
});
