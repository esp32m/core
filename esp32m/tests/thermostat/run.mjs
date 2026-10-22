import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(v => v && existsSync(v))
  ?? 'g++';
const root = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(root, 'esp32m-thermostat-'));
try {
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-static',
    '-I', path.join(core, 'include'), path.join(core, 'src/dev/thermostat/controller.cpp'),
    path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 20000 });
} finally {
  if (path.dirname(build) === root && path.basename(build).startsWith('esp32m-thermostat-'))
    rmSync(build, { recursive: true, force: true });
}
