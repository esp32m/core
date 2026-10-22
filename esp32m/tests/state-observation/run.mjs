import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
function between(text, start, end) {
  const first = text.indexOf(start), last = text.indexOf(end, first);
  if (first < 0 || last < 0) throw new Error(`Missing production section ${start}`);
  return text.slice(first, last);
}
const header = readFileSync(path.join(core, 'include/esp32m/app.hpp'), 'utf8');
const source = readFileSync(path.join(core, 'src/app.cpp'), 'utf8');
const declaration = between(header, '  class AppObject :', '  class App :');
const implementation = between(source, '  AppObject::AppObject()', '  static uint32_t sketchSize()');
const json = process.env.ARDUINOJSON_INCLUDE ?? path.resolve(core,
  '../../projects/greenhouse/managed_components/bblanchon__arduinojson/src');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe']
  .find(value => value && existsSync(value)) ?? process.env.CXX ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-state-observation-'));
try {
  writeFileSync(path.join(build, 'production.inc'),
    `namespace esp32m {\n${declaration}\n${implementation}\n}\n`);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-static', '-I', build, '-I', json,
    path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit' });
} finally {
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-state-observation-'))
    rmSync(build, { recursive: true, force: true });
}
