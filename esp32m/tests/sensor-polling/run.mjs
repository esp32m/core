import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const header = readFileSync(path.join(core, 'include/esp32m/device.hpp'), 'utf8');
const source = readFileSync(path.join(core, 'src/dev/device.cpp'), 'utf8');

function between(text, start, end) {
  const first = text.indexOf(start);
  const last = text.indexOf(end, first);
  if (first < 0 || last < 0) throw new Error(`Cannot locate production section: ${start}`);
  return text.slice(first, last);
}

// Keep the complete production Device declaration and sensor implementation
// unchanged. Unrelated component/emitter definitions and ESP-IDF includes are
// excluded; test.cpp supplies only their platform dependencies.
const device = between(header, '  class Device :', '  ENUM_FLAG_OPERATORS(Device::Flags)');
const scheduler = between(source, 'namespace esp32m {', '  namespace dev {');
const generated = `namespace esp32m {\n${device}\n}\n${scheduler}\n}\n`;
const candidates = [
  process.env.CXX,
  'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe',
];
const compiler = candidates.find((value) => value && existsSync(value)) ?? process.env.CXX ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-sensor-polling-'));
try {
  writeFileSync(path.join(build, 'production.inc'), generated);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, [
    '-std=c++17', '-Wall', '-Wextra', '-Werror', '-static', '-I', build,
    path.join(here, 'test.cpp'), '-o', exe,
  ], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit' });
} finally {
  // Verify the absolute target before recursively removing our own temp build.
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-sensor-polling-'))
    rmSync(build, { recursive: true, force: true });
}
