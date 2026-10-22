import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const json = process.env.ARDUINOJSON_INCLUDE ?? path.resolve(core,
  '../../projects/greenhouse/managed_components/bblanchon__arduinojson/src');
const header = readFileSync(path.join(core, 'include/esp32m/dev/opentherm.hpp'), 'utf8');
const source = readFileSync(path.join(core, 'src/dev/opentherm/opentherm.cpp'), 'utf8');
function between(text, start, end) {
  const first = text.indexOf(start);
  const last = text.indexOf(end, first);
  if (first < 0 || last < 0) throw new Error(`Missing production section: ${start}`);
  return text.slice(first, last);
}
const declaration = between(header, 'namespace esp32m {', '  namespace dev {') + '\n}\n';
const implementation = [
  between(source, '#ifdef __builtin_parity', '    bool Range::fromJson'),
  between(source, '    DataIdInfo IdInfoArray::get', '    bool IdInfoArray::fromJson'),
  between(source, '    void Message::init', '    void Recv::init'),
  between(source, '    esp_err_t Core::init', '    esp_err_t Core::receive'),
  between(source, '    esp_err_t Master::init', '    void Master::run'),
  between(source, '    bool GenericMaster::nextRequest', '    typedef opentherm::IDriver*'),
].join('\n');
const generated = declaration + '\nnamespace esp32m::opentherm {\n' + implementation +
  '\nvoid Master::run() {}\n}\n';
const actuatorBody = 'namespace esp32m::dev {\n' + between(source,
  '    thermostat::Capabilities OpenthermMaster::capabilities()',
  '    bool OpenthermMaster::handleRequest') + '\n}\n';
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(v => v && existsSync(v))
  ?? process.env.CXX ?? 'g++';
const build = mkdtempSync(path.join(here, '.build-'));
try {
  writeFileSync(path.join(build, 'production.inc'), generated);
  writeFileSync(path.join(build, 'actuator.inc'), actuatorBody);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-Wno-sign-compare', '-static', '-pthread',
    '-I', build, '-I', json, '-I', path.join(core, 'include'),
    path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 20000 });
} finally {
  if (path.dirname(build) === here && path.basename(build).startsWith('.build-'))
    rmSync(build, { recursive: true, force: true });
}
