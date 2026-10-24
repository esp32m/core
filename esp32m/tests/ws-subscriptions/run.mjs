import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const json = process.env.ARDUINOJSON_INCLUDE ?? path.resolve(core,
  '../../projects/scratch/managed_components/bblanchon__arduinojson/src');
if (!existsSync(path.join(json, 'ArduinoJson.h')))
  throw new Error('Set ARDUINOJSON_INCLUDE to ArduinoJson 7 src.');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(p => p && existsSync(p)) ?? 'g++';
const temp = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(temp, 'esp32m-ws-subscriptions-'));
try {
  const exe = path.join(build, 'test.exe');
  execFileSync(compiler, ['-std=c++17', '-fno-exceptions', '-Wall', '-Wextra', '-Werror', '-static',
    '-pthread', '-I', path.join(core, 'include'), '-I', json,
    path.join(core, 'src/ui/subscriptions.cpp'), path.join(here, 'test.cpp'),
    '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 20000 });
} finally {
  if (path.dirname(build) === temp && path.basename(build).startsWith('esp32m-ws-subscriptions-'))
    rmSync(build, { recursive: true, force: true });
}
