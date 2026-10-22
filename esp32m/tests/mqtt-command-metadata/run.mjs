import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const workspace = path.resolve(core, '../..');
const json = process.env.ARDUINOJSON_INCLUDE ?? path.join(workspace, 'projects/greenhouse/managed_components/bblanchon__arduinojson/src');
const netHeader = readFileSync(path.join(core, 'include/esp32m/net/mqtt.hpp'), 'utf8');
const source = readFileSync(path.join(core, 'src/net/mqtt.cpp'), 'utf8');
const haHeader = readFileSync(path.join(core, 'include/esp32m/integrations/ha/mqtt.hpp'), 'utf8');
const haEvents = readFileSync(path.join(core, 'include/esp32m/integrations/ha/ha.hpp'), 'utf8');
const slice = (source, start, end) => {
  const i = source.indexOf(start); const j = source.indexOf(end, i + start.length);
  if (i < 0 || j < 0) throw new Error(`Missing production marker ${start}`);
  return source.slice(i, j);
};
const pieces = {
  subscription: slice(netHeader, '      typedef std::function<void(std::string topic, std::string payload)>', '      struct Message {'),
  methods: slice(source, '    Subscription* Mqtt::subscribe(const char* topic, int qos)', '    void Mqtt::unsubscribe(') +
    slice(source, '    esp_err_t Mqtt::handle(', '    void Mqtt::disconnect()'),
  dev: slice(haHeader, '        class Dev {', '      }  // namespace mqtt'),
  command: slice(haEvents, '      class CommandRequest : public Request {', '    }  // namespace ha'),
};
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe', 'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe']
  .find(value => value && existsSync(value)) ?? 'g++';
const tempRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(tempRoot, 'esp32m-mqtt-metadata-'));
try {
  for (const [name, contents] of Object.entries(pieces)) writeFileSync(path.join(build, `${name}.inc`), contents);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-static', '-pthread',
    '-I', build, '-I', json, path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 20000 });
} finally {
  if (path.dirname(build) === tempRoot && path.basename(build).startsWith('esp32m-mqtt-metadata-'))
    rmSync(build, { recursive: true, force: true });
}
