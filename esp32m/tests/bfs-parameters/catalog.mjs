import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const workspace = path.resolve(core, '../..');
const json = process.env.ARDUINOJSON_INCLUDE ?? path.join(workspace,
  'projects/water-well/managed_components/bblanchon__arduinojson/src');
if (!existsSync(path.join(json, 'ArduinoJson.h')))
  throw new Error('Set ARDUINOJSON_INCLUDE to an ArduinoJson 7 src directory.');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(v => v && existsSync(v))
  ?? process.env.CXX ?? 'g++';
const target = path.join(core, 'web-ui/esp32m/ui/src/dev/bfs/catalog.ts');
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-bfs-catalog-'));
try {
  const exe = path.join(build, process.platform === 'win32' ? 'export.exe' : 'export');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-static',
    '-I', json, '-I', path.join(core, 'include'), path.join(here, 'export.cpp'),
    '-o', exe], { stdio: 'inherit' });
  const catalog = JSON.parse(execFileSync(exe, [], { encoding: 'utf8', timeout: 15000 }));
  const source = '// Generated from bfs/parameters.hpp; run tests/bfs-parameters/catalog.mjs --write.\n'
    + "import { TParameterDescriptor } from './types';\n\n"
    + `export const ParameterCatalog: TParameterDescriptor[] = ${JSON.stringify(catalog, null, 2)};\n`;
  if (process.argv.includes('--write')) {
    writeFileSync(target, source);
    console.log(`Generated ${catalog.length} BFS parameter descriptors.`);
  } else {
    if (!existsSync(target) || readFileSync(target, 'utf8').replace(/\r\n/g, '\n') !== source)
      throw new Error('BFS UI catalog is stale. Run tests/bfs-parameters/catalog.mjs --write.');
    console.log(`BFS UI catalog matches all ${catalog.length} canonical C++ descriptors.`);
  }
} finally {
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-bfs-catalog-'))
    rmSync(build, { recursive: true, force: true });
}
