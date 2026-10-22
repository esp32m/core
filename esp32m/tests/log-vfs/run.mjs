import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const header = readFileSync(path.join(core, 'include/esp32m/log/vfs.hpp'), 'utf8');
const source = readFileSync(path.join(core, 'src/log/vfs.cpp'), 'utf8');
const logging = readFileSync(path.join(core, 'src/log/logging.cpp'), 'utf8');
function between(text, start, end) {
  const first = text.indexOf(start), last = text.indexOf(end, first);
  if (first < 0 || last < 0) throw new Error(`Production anchor missing: ${start}`);
  return text.slice(first, last);
}
const formatting = between(logging, '    FormattingAppender::FormattingAppender(',
  '    bool isEmpty(');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(p => p && existsSync(p)) ?? 'g++';
// Windows AppContainer temp folders can deny native CRT rename. Use an
// isolated workspace directory so the host can exercise real rename/unlink.
const buildRoot = here;
const build = mkdtempSync(path.join(buildRoot, '.host-'));
try {
  writeFileSync(path.join(build, 'production.inc'),
    header.slice(header.indexOf('namespace esp32m {')) + '\n' +
    source.slice(source.indexOf('namespace esp32m {')) + '\n' +
    `namespace esp32m::log {\n${formatting}\n}\n`);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-static',
    '-pthread', '-I', build, path.join(here, 'test.cpp'), '-o', exe],
    { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', cwd: build, timeout: 15000 });
} finally {
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('.host-'))
    rmSync(build, { recursive: true, force: true });
}
