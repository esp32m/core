import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const read = name => readFileSync(path.join(core, name), 'utf8');
function between(text, first, last) {
  const start = text.indexOf(first), end = text.indexOf(last, start);
  if (start < 0 || end < 0) throw new Error(`Missing production section ${first}`);
  return text.slice(start, end);
}
const header = name => read(name).replace(/^\s*#(?:pragma|include).*$/gm, '');
const ui = read('src/ui/ui.cpp'), httpd = read('src/ui/httpd.cpp');
const source = header('include/esp32m/ui/client.hpp') + header('include/esp32m/ui/transport.hpp') +
  '\nvoid esp32m::Ui::process(ui::Transport& transport) { transport.process(); }\n' +
  between(ui, '    JsonDocument /*<JSON_ARRAY_SIZE(1)>*/ _errors;', '    class CaptureRequest :') + '\n' +
  between(ui, '    Client::Client(', '  }  // namespace ui');
const httpSource = between(httpd, '    esp_err_t Httpd::wsPreHandshake(',
  '    esp_err_t Httpd::authenticate(') +
  between(httpd, '    esp_err_t Httpd::sendTo(', '  }  // namespace ui');
const json = process.env.ARDUINOJSON_INCLUDE ?? path.resolve(core,
  '../../projects/scratch/managed_components/bblanchon__arduinojson/src');
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(p => p && existsSync(p)) ?? 'g++';
const temp = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(temp, 'esp32m-ws-lifecycle-'));
try {
  // Headers supply their own namespaces; implementation sections are extracted
  // byte-for-byte and compiled with deterministic fake HTTPD/platform services.
  const marker = source.indexOf('    JsonDocument /*<JSON_ARRAY_SIZE(1)>*/ _errors;');
  writeFileSync(path.join(build, 'production.inc'), source.slice(0, marker) +
    '\nnamespace esp32m::ui {\n' + source.slice(marker) + '\n}\n');
  writeFileSync(path.join(build, 'httpd.inc'), `namespace esp32m::ui {\n${httpSource}\n}\n`);
  const exe = path.join(build, 'test.exe');
  execFileSync(compiler, ['-std=c++20', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-static', '-pthread', '-I', build, '-I', json,
    '-I', path.join(core, 'include'),
    path.join(here, 'test.cpp'), '-o', exe], { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 20000 });
} finally {
  if (path.dirname(build) === temp && path.basename(build).startsWith('esp32m-ws-lifecycle-'))
    rmSync(build, { recursive: true, force: true });
}
