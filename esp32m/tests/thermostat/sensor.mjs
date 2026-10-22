import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const json = process.env.ARDUINOJSON_INCLUDE ?? path.resolve(core,
  '../../projects/greenhouse/managed_components/bblanchon__arduinojson/src');
const header = readFileSync(path.join(core, 'include/esp32m/device.hpp'), 'utf8');
const start = header.indexOf('    class Sensor :');
const end = header.indexOf('    class BinarySensor :', start);
if (start < 0 || end < 0) throw new Error('Sensor declaration missing');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(v => v && existsSync(v)) ?? 'g++';
const root = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(root, 'esp32m-thermostat-sensor-'));
try {
  writeFileSync(path.join(build, 'sensor.inc'),
    `namespace esp32m::dev {\n${header.slice(start, end)}\n}\n`);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-static',
    '-I', build, '-I', json, path.join(here, 'sensor.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 20000 });
} finally {
  if (path.dirname(build) === root && path.basename(build).startsWith('esp32m-thermostat-sensor-'))
    rmSync(build, { recursive: true, force: true });
}
