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

// Compile the complete production declaration and implementation unchanged.
// Only platform includes are supplied by the test's ESP-IDF/device doubles.
const header = readFileSync(path.join(core, 'include/esp32m/dev/sm10.hpp'), 'utf8');
const source = readFileSync(path.join(core, 'src/dev/sm10.cpp'), 'utf8');
const generated = `${header.slice(header.indexOf('namespace esp32m::dev {'))}\n` +
  source.slice(source.indexOf('namespace esp32m::dev {'));
const candidates = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'];
const compiler = candidates.find((value) => value && existsSync(value)) ?? process.env.CXX ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-sm10-driver-'));
try {
  writeFileSync(path.join(build, 'production.inc'), generated);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-static', '-pthread', '-I', build, '-I', json,
    '-I', path.join(core, 'include'), path.join(here, 'test.cpp'), '-o', exe],
    { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 20000 });
} finally {
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-sm10-driver-'))
    rmSync(build, { recursive: true, force: true });
}
