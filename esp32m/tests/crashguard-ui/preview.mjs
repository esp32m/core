// Render the production presentation with real React/MUI, without a device.
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const core = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(path.join(core, 'web-ui/package.json'));
const babel = require('@babel/core');
const react = require('react');
const { renderToStaticMarkup } = require('react-dom/server');
const material = require('@mui/material');
const files = {
  recovery: 'web-ui/esp32m/ui/src/debug/crashguard/recovery.tsx',
  types: 'web-ui/esp32m/ui/src/debug/crashguard/types.ts',
  system: 'web-ui/esp32m/ui/src/system/types.ts',
  values: 'web-ui/esp32m/ui/src/app/NameValueList.tsx',
};
const compiled = {};
for (const [name, file] of Object.entries(files)) {
  const filename = path.join(core, file);
  compiled[name] = (await babel.transformAsync(readFileSync(filename, 'utf8'), {
    filename, configFile: false, babelrc: false,
    presets: [[require.resolve('@babel/preset-typescript'), { onlyRemoveTypeImports: false }],
      [require.resolve('@babel/preset-react'), { runtime: 'automatic', development: false }]],
    plugins: [require.resolve('@babel/plugin-transform-modules-commonjs')],
  })).code;
}
const modules = {}, context = vm.createContext({ console });
function load(name) {
  if (modules[name]) return modules[name];
  const exports = modules[name] = {};
  vm.runInContext(`(function(exports, require) {${compiled[name]}\n})`, context)(exports, id => {
    if (['react', 'react/jsx-runtime', '@mui/material'].includes(id)) return require(id);
    if (id === '../../backend') return {};
    if (id === '../../app') return load('values');
    if (id === '../../system/types') return load('system');
    if (id === './types') return load('types');
    if (id === '@ts-libs/ui-i18n') return { useTranslation: () => ({ t: value => value }) };
    throw new Error(`Unexpected dependency ${id}`);
  });
  return exports;
}
const from = { slot: 1, ver: '1.0.79', sha256: 'deadbeef0123456789abcdef0123456789abcdef0123456789abcdef0123456789ab' };
const to = { slot: 0, ver: '1.0.78', sha256: '123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef01234' };
const incident = { active: true, status: 'fallback', boot: 75, reason: 'unexpected-reset',
  resetReason: 4, checkpoint: 20, uptime: 1234, strike: 10, early: 3, from, to };
const fallback = { supported: true, current: to, recovery: incident };
const testing = { ...fallback, current: { ...from, ver: '1.0.80', sha256: 'fedcba9876543210' },
  recovery: { ...incident, status: 'testing' } };
const { CrashGuardRecoveryBanner } = load('recovery');
const e = react.createElement;
const section = (title, state, width) => e('section', { style: { maxWidth: width, marginBottom: 32 } },
  e(material.Typography, { variant: 'h6', gutterBottom: true }, title),
  e(CrashGuardRecoveryBanner, { state }),
  e(material.Paper, { variant: 'outlined', style: { padding: 20 } },
    e(material.Typography, null, 'Current page content stays below the global notice.')));
const html = '<!doctype html><html lang="en"><meta charset="utf-8">' +
  '<meta name="viewport" content="width=device-width,initial-scale=1">' +
  '<title>CrashGuard recovery notice preview</title>' +
  '<style>body{margin:0;padding:24px;background:#f5f7fa}*{box-sizing:border-box}</style><body>' +
  renderToStaticMarkup(e(material.ThemeProvider, { theme: material.createTheme() },
    e(material.CssBaseline),
    section('Fallback firmware', fallback, 980),
    section('Replacement firmware evaluation', testing, 980),
    section('Narrow layout and same-version builds', { ...fallback, recovery: {
      ...incident, from: { ...from, ver: to.ver } } }, 375),
    section('Normal firmware: no recovery notice', { ...fallback, recovery: null }, 980))) +
  '</body></html>';
const output = path.resolve(core, '../ai/debug/crashguard/preview.html');
mkdirSync(path.dirname(output), { recursive: true });
writeFileSync(output, html);
console.log(`CrashGuard real React/MUI SSR preview: ${output}`);
