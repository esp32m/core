import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const source = readFileSync(path.join(core, 'src/net/wifi.cpp'), 'utf8');
function method(signature) {
  const start = source.indexOf(signature);
  if (start < 0) throw new Error(`Missing production ${signature}`);
  const opening = source.indexOf('{', start);
  let end = opening + 1;
  let depth = 1;
  for (; depth && end < source.length; ++end)
    depth += source[end] === '{' ? 1 : source[end] === '}' ? -1 : 0;
  if (depth) throw new Error(`Unclosed production ${signature}`);
  return source.slice(start, end);
}

const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe']
  .find(value => value && existsSync(value)) ?? process.env.CXX ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-wifi-roaming-'));
try {
  writeFileSync(path.join(build, 'connection.inc'), method('    bool Wifi::tryConnect() {'));
  writeFileSync(path.join(build, 'worker.inc'),
    method('    void Wifi::checkRoaming() {') + '\n' +
    method('    void Wifi::finishRoamingScan() {'));
  writeFileSync(path.join(build, 'migration.inc'), method('    bool Wifi::roamTo('));
  for (const name of ['test', 'integration', 'migration']) {
    const exe = path.join(build, process.platform === 'win32' ? `${name}.exe` : name);
    execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-static',
      '-I', build, '-I', path.join(core, 'include'), path.join(here, `${name}.cpp`),
      '-o', exe], { stdio: 'inherit' });
    execFileSync(exe, [], { stdio: 'inherit', timeout: 15000 });
  }
} finally {
  // Remove only the temporary build folder created immediately above.
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-wifi-roaming-'))
    rmSync(build, { recursive: true, force: true });
}
