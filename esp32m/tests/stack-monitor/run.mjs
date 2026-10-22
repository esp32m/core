import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe']
  .find(value => value && existsSync(value)) ?? process.env.CXX ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-stack-monitor-'));
try {
  for (const enabled of [1, 0]) {
    const exe = path.join(build, `test-${enabled}${process.platform === 'win32' ? '.exe' : ''}`);
    execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-static',
      `-DCONFIG_ESP32M_STACK_MONITOR=${enabled}`, '-I', path.join(here, 'fakes'),
      '-I', path.join(core, 'include'), path.join(here, 'test.cpp'), '-o', exe],
      { stdio: 'inherit' });
    execFileSync(exe, [], { stdio: 'inherit' });
  }
} finally {
  // Only remove this script's verified temporary build directory.
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-stack-monitor-'))
    rmSync(build, { recursive: true, force: true });
}
