import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, '../..');
const source = readFileSync(path.join(core, 'src/ui/httpd.cpp'), 'utf8');
function between(start, end) {
  const first = source.indexOf(start), last = source.indexOf(end, first);
  if (first < 0 || last < 0) throw new Error(`Production anchor missing: ${start}`);
  return source.slice(first, last);
}
const sections = [
  between('    bool uriMatcher(', '    esp_err_t wsHandler('),
  between('    esp_err_t Httpd::httpHandlerCustom(', '    esp_err_t Httpd::wsHandlerCustom('),
  between('    esp_err_t Httpd::wsHandlerCustom(', '    void Httpd::wsSessionClosed('),
  between('    void Httpd::wsSessionClosed(', '    esp_err_t httpHandler('),
  between('    esp_err_t Httpd::wsPreHandshake(', '    esp_err_t Httpd::wsPostHandshake('),
  between('    void Httpd::prepareWsHandler(', '    esp_err_t Httpd::incomingWs('),
  between('    static char* http_auth_basic(', '    esp_err_t getHeader('),
  between('    esp_err_t Httpd::authenticate(', '    esp_err_t Httpd::sendTo('),
];
const compiler = [process.env.CXX, 'C:/work/tools/mingw64/bin/g++.exe',
  'C:/work/tools/git-sdk-64/mingw64/bin/g++.exe'].find(p => p && existsSync(p)) ?? 'g++';
const buildRoot = path.resolve(os.tmpdir());
const build = mkdtempSync(path.join(buildRoot, 'esp32m-httpd-access-'));
try {
  writeFileSync(path.join(build, 'production.inc'),
    `namespace esp32m::ui {\n${sections.join('\n')}\n}\n`);
  const exe = path.join(build, process.platform === 'win32' ? 'test.exe' : 'test');
  execFileSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-static', '-pthread', '-I', build,
    '-I', path.join(core, 'include'), path.join(here, 'test.cpp'), '-o', exe],
    { stdio: 'inherit' });
  execFileSync(exe, [], { stdio: 'inherit', timeout: 15000 });
} finally {
  if (path.dirname(build) === buildRoot && path.basename(build).startsWith('esp32m-httpd-access-'))
    rmSync(build, { recursive: true, force: true });
}
