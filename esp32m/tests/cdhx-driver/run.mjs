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

// Keep all production C++ bodies, including anonymous-namespace helpers.
// The test supplies only ESP-IDF/framework platform doubles and a fake clock.
const production = ['include/esp32m/dev/cdhx.hpp', 'src/dev/cdhx.cpp']
  .map(file => readFileSync(path.join(core, file), 'utf8')
    .replace(/^\s*#\s*(?:include[^\n]*|pragma once)\r?\n/gm, ''))
  .join('\n');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(value => value && existsSync(value))
  ?? process.env.CXX ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-cdhx-driver-'));
try {
  writeFileSync(path.join(build, 'production.inc'), production);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-static', '-pthread', '-I', build, '-I', json,
    '-I', path.join(core, 'include'), path.join(here, 'test.cpp'), '-o', exe],
    { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 20000 });
} finally {
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-cdhx-driver-'))
    rmSync(build, { recursive: true, force: true });
}
