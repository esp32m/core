import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const workspace = path.resolve(core, '../..');
const json = process.env.ARDUINOJSON_INCLUDE ?? path.join(workspace,
  'projects/greenhouse/managed_components/bblanchon__arduinojson/src');
if (!existsSync(path.join(json, 'ArduinoJson.h')))
  throw new Error('Set ARDUINOJSON_INCLUDE to an ArduinoJson 7 src directory.');
const read = file => readFileSync(path.join(core, file), 'utf8').replaceAll('\r\n', '\n');
function between(source, first, last) {
  const start = source.indexOf(first), end = source.indexOf(last, start);
  if (start < 0 || end < 0) throw new Error(`Missing production section ${first}`);
  return source.slice(start, end);
}
function namespaceBody(source) {
  const start = source.indexOf('namespace esp32m::log {');
  if (start < 0) throw new Error('Missing production log namespace');
  return source.slice(start + 'namespace esp32m::log {'.length, source.lastIndexOf('}'));
}
const loggingHeader = read('include/esp32m/logging.hpp');
const loggingSource = read('src/log/logging.cpp');
const storeHeader = read('include/esp32m/log/store.hpp');
const declaration = between(loggingHeader,
  '    struct __attribute__((packed)) LogMessage {',
  '    /**\n     * @brief Logger implementation');
const allocator = between(loggingSource, '    LogMessage* LogMessage::alloc(',
  '    Logger& Loggable::logger()');
const formatter = between(loggingSource, '    char* format(const LogMessage* msg)',
  '    FormattingAppender::FormattingAppender');
const store = between(storeHeader, '  class Store :', '}  // namespace esp32m::log');
const source = `namespace esp32m::log {\n${declaration}\n${allocator}\n${formatter}\n` +
  `${namespaceBody(read('src/log/message.cpp'))}\n${store}\n` +
  `${namespaceBody(read('src/log/store.cpp'))}\n}\n`;
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(v => v && existsSync(v))
  ?? process.env.CXX ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-log-store-'));
try {
  writeFileSync(path.join(build, 'production.inc'), source);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-static', '-pthread', '-I', build, '-I', json,
    '-I', path.join(core, 'include'), path.join(core, 'src/log/storage.cpp'),
    path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [build.replaceAll('\\', '/')], { stdio: 'inherit', timeout: 30000 });
} finally {
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-log-store-'))
    rmSync(build, { recursive: true, force: true });
}
