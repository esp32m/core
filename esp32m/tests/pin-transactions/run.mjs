import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
function between(text, start, end) {
  const first = text.indexOf(start);
  const last = text.indexOf(end, first);
  if (first < 0 || last < 0) throw new Error(`Cannot locate production section: ${start}`);
  return text.slice(first, last);
}
const pinHeader = readFileSync(path.join(core, 'include/esp32m/io/pins.hpp'), 'utf8');
const pinSource = readFileSync(path.join(core, 'src/io/pins.cpp'), 'utf8');
const bridgeSource = readFileSync(path.join(core, 'src/dev/hbridge.cpp'), 'utf8');
const muxSource = readFileSync(path.join(core, 'src/io/mux74hc.cpp'), 'utf8');
// Compile the full, unchanged production Tx declaration/implementation and
// HBridge hardware methods. Platform services and GPIO hardware are fakes.
const txDeclaration = between(pinHeader, '      class ITxFinalizer {', '    }  // namespace pin');
const txMethods = between(pinSource, '      thread_local Tx *Tx::_current', '      PcntSampler::PcntSampler(');
const bridgeMethods = between(bridgeSource, '    esp_err_t HBridge::init()', '    JsonDocument *HBridge::getState(');
const muxMethods = between(muxSource, '    esp_err_t Mux74hc::selectChannel(', '    namespace mux74hc {');
const candidates = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'];
const compiler = candidates.find((value) => value && existsSync(value)) ?? process.env.CXX ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-pin-transactions-'));
try {
  writeFileSync(path.join(build, 'production-tx.inc'),
    `namespace esp32m::io::pin {\n${txDeclaration}\n${txMethods}\n}\n`);
  writeFileSync(path.join(build, 'production-hardware.inc'),
    `namespace esp32m::dev {\n${bridgeMethods}\n}\nnamespace esp32m::io {\n${muxMethods}\n}\n`);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-static',
    '-pthread', '-I', build, path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 15000 });
} finally {
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-pin-transactions-'))
    rmSync(build, { recursive: true, force: true });
}
