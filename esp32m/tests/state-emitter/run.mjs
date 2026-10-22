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

// Compile the unchanged production declaration, event handler and task loop.
// Only platform, component registry and output transports are replaced.
const declaration = between(header, '    enum EmitFlags', '    class Sensor :');
const implementation = between(source, '    StateEmitter::StateEmitter', '  }  // namespace dev');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe']
  .find((value) => value && existsSync(value)) ?? process.env.CXX ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-state-emitter-'));
try {
  writeFileSync(path.join(build, 'production.inc'),
    `namespace esp32m::dev {\n${declaration}\n${implementation}\n}\n`);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-static', '-pthread', '-I', build,
    path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit' });
} finally {
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-state-emitter-'))
    rmSync(build, { recursive: true, force: true });
}
