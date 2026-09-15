import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const candidates = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'];
const compiler = candidates.find(value => value && existsSync(value)) ?? process.env.CXX ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-i2c-recovery-'));
try {
  // Compile the real header and i2c.cpp. Replace only platform dependencies.
  for (const file of ['driver/i2c_master.h', 'freertos/FreeRTOS.h', 'freertos/task.h',
    'esp32m/base.hpp', 'esp32m/defs.hpp', 'esp32m/logging.hpp']) {
    const target = path.join(build, file);
    mkdirSync(path.dirname(target), { recursive: true });
    writeFileSync(target, '#include "platform.hpp"\n');
  }
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread',
    '-static', '-I', build, '-I', here, '-I', path.join(core, 'include'),
    path.join(here, 'test.cpp'), path.join(core, 'src/bus/i2c.cpp'), '-o', exe],
    { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 15000 });
} finally {
  // Delete only this runner's verified temporary build directory.
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-i2c-recovery-'))
    rmSync(build, { recursive: true, force: true });
}
