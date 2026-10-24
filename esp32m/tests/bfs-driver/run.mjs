import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const workspace = path.resolve(core, '../..');
const json = process.env.ARDUINOJSON_INCLUDE ?? path.join(workspace,
  'projects/greenhouse/managed_components/bblanchon__arduinojson/src');
if (!existsSync(path.join(json, 'ArduinoJson.h')))
  throw new Error('Set ARDUINOJSON_INCLUDE to an ArduinoJson 7 src directory.');
const production = ['include/esp32m/dev/bfs.hpp', 'src/dev/bfs.cpp']
  .map(file => {
    const source = readFileSync(path.join(core, file), 'utf8');
    const start = source.indexOf('namespace esp32m');
    if (start < 0) throw new Error(`Missing namespace in ${file}`);
    return source.slice(start);
  }).join('\n');
const uiSource = readFileSync(path.join(core, 'src/ui/ui.cpp'), 'utf8');
const serializerStart = uiSource.indexOf('    Payload makeResponse(');
const serializerEnd = uiSource.indexOf('    class Rb', serializerStart);
if (serializerStart < 0 || serializerEnd < 0)
  throw new Error('Could not locate production UI response serializer.');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(v => v && existsSync(v))
  ?? process.env.CXX ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-bfs-driver-'));
try {
  writeFileSync(path.join(build, 'production.inc'), production);
  writeFileSync(path.join(build, 'serializer.inc'),
    `namespace esp32m::ui {\n${uiSource.slice(serializerStart, serializerEnd)}\n}\n`);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-static', '-pthread', '-I', build, '-I', json,
    '-I', path.join(core, 'include'), path.join(here, 'test.cpp'), '-o', exe],
    { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 20000 });
  if (process.argv[2]) {
    const fixture = execFileSync(exe, ['--fixture'], { encoding: 'utf8', timeout: 20000 });
    writeFileSync(path.resolve(process.argv[2]), `${JSON.stringify(JSON.parse(fixture), null, 2)}\n`);
  }
} finally {
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-bfs-driver-'))
    rmSync(build, { recursive: true, force: true });
}
