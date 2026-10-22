import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const source = readFileSync(path.join(core, 'src/dev/lf-bros.cpp'), 'utf8');
// Compile the actual production bodies with a fake serialized CAN transport.
// Braces in comments/strings are skipped, so extraction cannot silently truncate.
function block(marker) {
  const start = source.indexOf(marker);
  if (start < 0) throw new Error(`Missing production block ${marker}`);
  let end = source.indexOf('{', start), depth = 0;
  for (; end < source.length; ++end) {
    const ch = source[end], next = source[end + 1];
    if (ch === '/' && next === '/') { end = source.indexOf('\n', end); continue; }
    if (ch === '/' && next === '*') { end = source.indexOf('*/', end + 2) + 1; continue; }
    if (ch === '"' || ch === "'") {
      const quote = ch;
      for (++end; end < source.length && source[end] !== quote; ++end)
        if (source[end] === '\\') ++end;
      continue;
    }
    if (ch === '{') ++depth;
    if (ch === '}' && --depth === 0) return source.slice(start, end + 1);
  }
  throw new Error(`Unterminated production block ${marker}`);
}
const declarations = ['struct Desired {', 'struct Runtime {', 'enum class CommandType : uint8_t {', 'struct Command {']
  .map(marker => `${block(marker)};`).join('\n');
const methods = ['Desired desired()', 'Desired effectiveDesired()', 'void manualTakeover()',
  'thermostat::Capabilities thermostatCapabilities()', 'thermostat::Feedback thermostatSnapshot()',
  'thermostat::Result thermostatApply(', 'void setThermostatAttached(', 'void expireThermostat()', 'void observeThermostat(',
  'bool setDesired(', 'esp_err_t setTemperature(', 'esp_err_t setPower(', 'esp_err_t setControlMode(',
  'esp_err_t turn(', 'esp_err_t enqueue(', 'bool runtimeIsStale(', 'void processCommands()',
  'bool isStandbyOff()', 'esp_err_t applyDesiredSetpoint()', 'void updateConnection()']
  .map(block).join('\n');
const forwarders = ['thermostat::Capabilities Lfbros::capabilities()',
  'thermostat::Feedback Lfbros::snapshot()', 'thermostat::Result Lfbros::apply(',
  'void Lfbros::thermostatAttached('].map(block).join('\n');
const header = readFileSync(path.join(core, 'include/esp32m/dev/lf-bros.hpp'), 'utf8')
  .replace(/^\s*#\s*(?:include[^\n]*|pragma once)\r?\n/gm, '');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(value => value && existsSync(value)) ?? 'g++';
const build = mkdtempSync(path.join(os.tmpdir(), 'esp32m-lfbros-thermostat-'));
try {
  writeFileSync(path.join(build, 'header.inc'), header);
  writeFileSync(path.join(build, 'declarations.inc'), declarations);
  writeFileSync(path.join(build, 'methods.inc'), methods);
  writeFileSync(path.join(build, 'forwarders.inc'), forwarders);
  const exe = path.join(build, 'test.exe');
  execFileSync(compiler, ['-std=c++23', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
    '-static', '-pthread', '-I', build, '-I', path.join(core, 'include'),
    path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 10000 });
} finally {
  if (path.basename(build).startsWith('esp32m-lfbros-thermostat-')) rmSync(build, { recursive: true, force: true });
}
