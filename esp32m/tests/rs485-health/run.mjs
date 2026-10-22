import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url));
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe'].find(p => p && existsSync(p)) ?? 'g++';
const root = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(root, 'esp32m-rs485-health-'));
try {
  const executable = path.join(build, 'test.exe');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', '-static',
    '-I', path.resolve(here, '../../include'), path.join(here, 'test.cpp'), '-o', executable], { stdio: 'inherit' });
  execFileSync(executable, [], { stdio: 'inherit', timeout: 15000 });
} finally {
  if (path.dirname(build) === root && path.basename(build).startsWith('esp32m-rs485-health-'))
    rmSync(build, { recursive: true, force: true });
}
