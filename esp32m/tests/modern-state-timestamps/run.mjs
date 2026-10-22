import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
function between(text, start, end) {
  const first = text.indexOf(start), last = text.indexOf(end, first);
  if (first < 0 || last < 0) throw new Error(`Missing production section ${start}`);
  return text.slice(first, last);
}
const fan = readFileSync(path.join(core, 'src/dev/fan.cpp'), 'utf8');
const valve = readFileSync(path.join(core, 'src/dev/flmvalve.cpp'), 'utf8');
const rs485 = readFileSync(path.join(core, 'src/debug/rs485.cpp'), 'utf8');
const source = `namespace esp32m::dev {\n` +
  between(fan, '    JsonDocument* Fan::getState(', '    void Fan::setState(') +
  between(fan, '    bool Fan::pollSensors()', '    bool Fan::setConfig(') +
  between(valve, '    JsonDocument* FlomarvelValve::getState(', '    FlomarvelValve* useFlomarvelValve(') +
  `\n}\nnamespace esp32m::debug {\n` +
  between(rs485, '  namespace {', '  Rs485& Rs485::instance()') +
  between(rs485, '  JsonDocument* Rs485::getState(', '  bool Rs485::handleRequest(') + '\n}\n';
const json = process.env.ARDUINOJSON_INCLUDE ?? path.resolve(core,
  '../../projects/scratch/managed_components/bblanchon__arduinojson/src');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(p => p && existsSync(p)) ?? 'g++';
const temp = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(temp, 'esp32m-modern-state-timestamps-'));
try {
  writeFileSync(path.join(build, 'production.inc'), source);
  const exe = path.join(build, 'test.exe');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-static', '-pthread', '-I', build, '-I', json,
    '-I', path.join(core, 'include'), path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 20000 });
} finally {
  if (path.dirname(build) === temp && path.basename(build).startsWith('esp32m-modern-state-timestamps-'))
    rmSync(build, { recursive: true, force: true });
}
