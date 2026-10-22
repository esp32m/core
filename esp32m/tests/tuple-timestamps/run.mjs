import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const read = file => readFileSync(path.join(core, file), 'utf8');
function method(file, signature) {
  const source = read(file), start = source.indexOf(signature);
  if (start < 0) throw new Error(`Missing production method ${signature}`);
  const opening = source.indexOf('{', start);
  let depth = 1, end = opening + 1;
  for (; depth && end < source.length; ++end)
    depth += source[end] === '{' ? 1 : source[end] === '}' ? -1 : 0;
  if (depth) throw new Error(`Unclosed production method ${signature}`);
  return source.slice(start, end);
}
const classes = {
  fc37: 'Fc37', ptmb: 'Ptmb', cap11xx: 'Cap11xx', mlx90614: 'Mlx90614',
  flow: 'FlowSensor', mwms: 'MicrowaveMotionSensor',
};
let implementation = '', stamps = '';
for (const [file, name] of Object.entries(classes)) {
  const header = read(`include/esp32m/dev/${file}.hpp`);
  const field = header.match(/int64_t _stamp = -1;/)?.[0];
  if (!field) throw new Error(`${file} acquisition stamp must distinguish never-read and zero`);
  stamps += `#define STAMP_${file} ${field}\n`;
  implementation += method(`src/dev/${file}.cpp`, `${name}::getState(`) + '\n';
  if (!['cap11xx'].includes(file))
    implementation += method(`src/dev/${file}.cpp`, `${name}::pollSensors(`) + '\n';
  if (file === 'mwms') implementation += method('src/dev/mwms.cpp', `${name}::run(`) + '\n';
  if (file === 'flow') implementation += method('src/dev/flow.cpp', `${name}::compute(`) + '\n';
}
// Keep the actual return type, which precedes the qualified name.
implementation = implementation.replace(/(^|\n)(\w+::getState\()/g, '$1JsonDocument* $2')
  .replace(/(^|\n)(\w+::pollSensors\()/g, '$1bool $2')
  .replace(/(^|\n)(MicrowaveMotionSensor::run\()/g, '$1void $2')
  .replace(/(^|\n)(FlowSensor::compute\()/g, '$1float $2');
const json = process.env.ARDUINOJSON_INCLUDE ?? path.resolve(core,
  '../../projects/scratch/managed_components/bblanchon__arduinojson/src');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(p => p && existsSync(p)) ?? 'g++';
const temp = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(temp, 'esp32m-tuple-timestamps-'));
try {
  writeFileSync(path.join(build, 'stamps.inc'), stamps);
  writeFileSync(path.join(build, 'production.inc'), `namespace esp32m::dev {\n${implementation}\n}\n`);
  const exe = path.join(build, 'test.exe');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-static', '-pthread', '-I', build, '-I', json,
    path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 15000 });
} finally {
  if (path.dirname(build) === temp && path.basename(build).startsWith('esp32m-tuple-timestamps-'))
    rmSync(build, { recursive: true, force: true });
}
