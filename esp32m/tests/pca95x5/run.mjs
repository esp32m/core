import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
function between(text, start, end) {
  const first = text.indexOf(start), last = text.indexOf(end, first);
  if (first < 0 || last < 0) throw new Error(`Production anchor missing: ${start}`);
  return text.slice(first, last);
}
const header = readFileSync(path.join(core, 'include/esp32m/io/pca95x5.hpp'), 'utf8');
const source = readFileSync(path.join(core, 'src/io/pca95x5.cpp'), 'utf8');
const pinHeader = readFileSync(path.join(core, 'include/esp32m/io/pins.hpp'), 'utf8');
const pinSource = readFileSync(path.join(core, 'src/io/pins.cpp'), 'utf8');
const txDeclaration = between(pinHeader, '      class ITxFinalizer {', '    }  // namespace pin');
const txMethods = between(pinSource, '      thread_local Tx *Tx::_current', '      PcntSampler::PcntSampler(');
const declaration = header.slice(header.indexOf('namespace esp32m {'));
const implementation = source.slice(source.indexOf('    Pca95x5::Pca95x5('));
if (!declaration.includes('class Pca95x5') || !implementation.includes('Pca95x5::commit'))
  throw new Error('Production PCA anchors changed');
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-pca95x5-'));
try {
  // Production class/method bodies remain unchanged; only hardware/platform
  // dependencies and the unused per-pin feature adapter are replaced.
  writeFileSync(path.join(build, 'production.inc'),
    `namespace esp32m::io::pin {\n${txDeclaration}\n${txMethods}\n}\n${declaration}\n` +
    `namespace esp32m { namespace io { namespace pca95x5 { class Pin : public IPin { public: Pin(Pca95x5*, int) {} }; }\n${implementation}`);
  const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
    'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(p => p && existsSync(p)) ?? 'g++';
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-static',
    '-pthread', '-I', build, path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 15000 });
} finally {
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-pca95x5-'))
    rmSync(build, { recursive: true, force: true });
}
