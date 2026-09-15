import { CleanWebpackPlugin } from 'clean-webpack-plugin';
import CompressionPlugin from 'compression-webpack-plugin';
import CopyWebpackPlugin from 'copy-webpack-plugin';
import HtmlWebpackPlugin from 'html-webpack-plugin';
import path from 'path';
import TerserPlugin from 'terser-webpack-plugin';
import { PackageJson } from 'types-package-json';
import { BundleAnalyzerPlugin } from 'webpack-bundle-analyzer';
//import WebpackStringReplacer from 'webpack-string-replacer';

import { existsSync } from 'fs';
import {
  Configuration,
  DefinePlugin,
  ModuleOptions,
  optimize,
  RuleSetRule,
  webpack,
  WebpackPluginInstance,
} from 'webpack';
import 'webpack-dev-server';

enum Destination {
  Local = 'local',
  Remote = 'remote',
  Analyze = 'analyze',
}

function ToDestination(v: string | undefined): Destination | undefined {
  if (!v) return;
  const a = [Destination.Local, Destination.Remote, Destination.Analyze];
  const i = a.indexOf(v as Destination);
  if (i >= 0) return a[i];
}

enum Mode {
  None = 'none',
  Development = 'development',
  Production = 'production',
}

function ToMode(v: string | undefined): Mode | undefined {
  if (!v) return;
  const a = [Mode.None, Mode.Development, Mode.Production];
  const i = a.indexOf(v as Mode);
  if (i >= 0) return a[i];
}

function findModuleRule(rules: ModuleOptions['rules'], loader: string) {
  return Array.isArray(rules)
    ? rules.find(
      (r) =>
        r != '...' &&
        r &&
        (r.loader == loader ||
          (Array.isArray(r.use) &&
            r.use.some((u) => u == loader || (u as any)['loader'] == loader)))
    )
    : undefined;
}

const expressViewsFixLoader = {
  test: /view\.js$/,
  loader: 'string-replace-loader',
  options: {
    search: 'require(mod).__express',
    replace: 'function() { throw "views are not implemented"; }',
  },
};
const muiLicenseFixLoader = {
  test: /Watermark\.js$/,
  loader: 'string-replace-loader',
  options: {
    search: 'function getLicenseErrorMessage(licenseStatus) {',
    replace: 'function getLicenseErrorMessage(licenseStatus) { return ""; } function _getLicenseErrorMessage(licenseStatus) {',
  },
};
// webpack 5.110.2 incorrectly validates MUI imports that become unused after
// production PropTypes removal (webpack/webpack#21856). Limit the workaround
// to MUI; keep export-presence validation enabled for application code.
const muiUnusedImportWorkaround = {
  test: /\.mjs$/,
  include: /node_modules[\\/]@mui[\\/]/,
  parser: { exportsPresence: false },
};
const ssdpOptionsFixLoader = {
  test: /default-ssdp-options\.js$/,
  loader: 'string-replace-loader',
  options: {
    search: "const pkg = req('../../package.json')",
    replace: "import pkg from '../../package.json'",
  },
};
const bindingsFixLoader = {
  test: /bindings\.js$/,
  loader: 'string-replace-loader',
  options: {
    search: 'opts.module_root = ',
    replace: 'opts.module_root = __dirname; //',
  },
};

const cssLoader = {
  test: /\.css$/i,
  use: ['style-loader', 'css-loader'],
};

/*const nodeGypFixLoader = {
  test: /node-gyp-build\.js$/,
  loader: 'string-replace-loader',
  options: {
    search: /path\.join\(dir, 'prebuilds'/g,
    replace: "path.join(__dirname, 'prebuilds'",
  },
};*/

const serialportFixLoaders = [
  {
    test: /load-bindings\.js$/,
    loader: 'string-replace-loader',
    options: {
      search: /\(__dirname, '\.\.\/'\)/g,
      replace: "(__dirname, './native/serialport')",
    },
  },
  {
    test: /poller\.js$/,
    loader: 'string-replace-loader',
    options: {
      search: /\(__dirname, '\.\.\/'\)/g,
      replace: "(__dirname, './native/serialport')",
    },
  },
];
const ipfsUtilsHttpFixLoader = (node: boolean) => ({
  test: /fetch\.js$/,
  loader: 'string-replace-loader',
  options: {
    multiple: [
      {
        search: 'require(implName)',
        replace: `require('./fetch.${node ? 'node' : 'browser'}')`,
      },
      {
        search: 'require(impl)',
        replace: "require('native-fetch')",
      },
    ],
  },
});

const inlineImageLoader = {
  test: /\.(png|jpg|gif)$/i,
  type: 'asset/inline',
  /*use: [
    {
      loader: 'url-loader',
      options: {
        limit: 8192,
      },
    },
  ],*/
};

const inlineJsonLoader = {
  test: /\.json$/i,
  type: 'asset/source',
};

type BuilderConfig = {
  defines?: Record<string, any>;
};
export class WebpackConfigBuilder {
  readonly baseConfig: Configuration;
  readonly config: Configuration = {};
  readonly dir: string;
  package: PackageJson;
  destination: Destination | undefined;
  constructor(
    dir: string,
    config?: Configuration,
    readonly builderConfig?: BuilderConfig
  ) {
    this.dir = dir;
    this.baseConfig = Object.assign({}, config);
    this.package = require(path.join(dir, 'package.json'));
  }
  setTarget(target: Configuration['target']) {
    this.config.target = target;
    return this;
  }
  isDevelopment() {
    return this.config.mode == Mode.Development;
  }
  isProduction() {
    return this.config.mode == Mode.Production;
  }
  hasWebTargets() {
    const { target } = this.config;
    return !target || (Array.isArray(target) && target.includes('web'));
  }
  hasNodeTarget() {
    const { target } = this.config;
    return (
      target == 'node' || (Array.isArray(target) && target.includes('node'))
    );
  }
  private findModule(name: string, tester?: (path: string) => boolean) {
    let curdir = this.dir;
    for (; ;) {
      const dir = path.resolve(curdir, `./node_modules/${name}`);
      if (existsSync(dir) && (!tester || tester(dir))) return dir;
      const parent = path.resolve(curdir, '..');
      if (parent == curdir) break;
      curdir = parent;
    }
  }
  private buildBabelLoader() {
    let targets: any;
    if (this.hasNodeTarget()) targets = { node: true };
    else if (this.hasWebTargets()) targets = { browsers: '> 5%' };
    const envPreset: any = [
      '@babel/env',
      {
        modules: false,
        targets,
      },
    ];
    // Babel 8 removed the isTSX/allExtensions options from
    // @babel/preset-typescript, and it no longer prevents `<T>() => {}`
    // generic arrow functions in plain `.ts` files from being parsed as
    // ambiguous JSX when @babel/preset-react's JSX syntax plugin is also
    // active. onlyRemoveTypeImports must also be explicit: without it,
    // Babel 8.0.x doesn't elide type-only imports (e.g. `import type`-like
    // usages of `import { X } from 'y'`), leaving them in the bundle and
    // causing webpack to fail resolving types-only modules.
    const tsPreset: any = [
      '@babel/typescript',
      { optimizeConstEnums: true, onlyRemoveTypeImports: false },
    ];
    const reactPreset: any = ['@babel/react', { runtime: 'automatic' }];
    const hasReact = !!this.package.dependencies?.['react'];
    const plugins: Array<any> = [
      ['@babel/plugin-proposal-decorators', { version: 'legacy' }],
    ];
    if (this.package.dependencies?.['lodash']) plugins.push('lodash');
    const makeBabelUse = (withReact: boolean) => ({
      loader: 'babel-loader',
      options: {
        presets:
          withReact && hasReact
            ? [envPreset, tsPreset, reactPreset]
            : [envPreset, tsPreset],
        plugins,
      },
    });
    return {
      test: /\.tsx?$/,
      exclude: /node_modules/,
      oneOf: [
        { test: /\.tsx$/, use: [makeBabelUse(true)] },
        { use: [makeBabelUse(false)] },
      ],
    };
  }
  private buildModule() {
    const { module = {} } = this.baseConfig;
    const { rules = [], ...rest } = module;
    const built: RuleSetRule[] = [];
    const babel = findModuleRule(rules, 'babel-loader');
    if (!babel) built.push(this.buildBabelLoader());
    if (this.package.dependencies?.['express']) {
      built.push(expressViewsFixLoader);
    }
    if (this.package.dependencies?.['ipfs-core']) {
      built.push(ipfsUtilsHttpFixLoader(this.hasNodeTarget()));
      built.push(ssdpOptionsFixLoader);
      built.push(inlineJsonLoader);
    }
    built.push(muiLicenseFixLoader);
    built.push(muiUnusedImportWorkaround);
    built.push(bindingsFixLoader);
    built.push(cssLoader);
    built.push(...serialportFixLoaders);
    const url = findModuleRule(rules, 'url-loader');
    if (!url && this.hasWebTargets()) built.push(inlineImageLoader);
    // Allow extensionless imports from strict ESM modules.
    built.push({ test: /\.m?js$/, resolve: { fullySpecified: false } });
    return { rules: [...rules, ...built], ...rest };
  }
  private buildOptimization() {
    const { optimization = {} } = this.baseConfig;
    const minimize = this.isProduction();
    const minimizer = [
      new TerserPlugin({
        parallel: true,
        terserOptions: {
          keep_classnames: /AbortSignal/,
          keep_fnames: /AbortSignal/,
          output: { comments: false },
        },
      }),
    ];
    return { minimize, minimizer, ...optimization };
  }
  private buildPerformance() {
    const { performance = {} } = this.baseConfig;
    const limit = 16 * 1024 * 1024;
    return { maxEntrypointSize: limit, maxAssetSize: limit, ...performance };
  }
  private buildResolve() {
    const { resolve = {} } = this.baseConfig;
    const alias: Record<string, any> = this.hasNodeTarget()
      ? { 'node-fetch': 'node-fetch/lib/index.js' }
      : {
        os: false,
        fs: false,
        path: false,
        express: false,
        ws: false,
        winston: false,
        debug: false,
        'express-ws': false,
        'node-localstorage': false,
        'redux-persist-node-storage': false,
      };
    //    const fallback=this.hasNodeTarget()?{}:{crypto: require.resolve('crypto-browserify')};
    if (this.package.dependencies?.['puppeteer'])
      alias['pkg-dir'] = '@ts-libs/stubs';
    const extensions = ['.tsx', '.ts', '.js', '.jsx'];
    /*    const mainFields = this.hasNodeTarget()
      ? {}
      : { mainFields: ['main', 'module'] };*/
    return { extensions, alias, ...resolve };
  }
  private buildInfoPlugin() {
    const globalObjectName = this.hasNodeTarget() ? 'global' : 'window';
    return new DefinePlugin({
      [`${globalObjectName}.__build_info`]: {
        version: JSON.stringify(this.package.version),
        built: JSON.stringify(new Date().toISOString()),
        mode: JSON.stringify(this.config.mode),
        destination: JSON.stringify(this.destination),
      },
      ...Object.fromEntries(
        Object.entries(this.builderConfig?.defines || {}).map(([k, v]) => [
          `${globalObjectName}.${k}`,
          JSON.stringify(v),
        ])
      ),
    });
  }
  private buildPlugins() {
    const { plugins = [] } = this.baseConfig;
    const buildInfo = this.buildInfoPlugin();
    const result: Array<WebpackPluginInstance> = [buildInfo];
    if (this.hasWebTargets()) {
      result.push(
        new HtmlWebpackPlugin({
          title: 'app',
          template: 'src/index.html',
        })
      );
      if (this.isProduction())
        result.push(
          new CleanWebpackPlugin({}),
          new CompressionPlugin({
            test: /\.(js|css|html|svg)$/,
            algorithm: 'gzip',
          })
        );
    }
    if (this.hasNodeTarget()) {
      result.push(
        new optimize.LimitChunkCountPlugin({
          maxChunks: 1,
        })
      );
      const serialPort = this.findModule('@serialport', (p) =>
        existsSync(path.resolve(p, './bindings-cpp/prebuilds'))
      );
      if (serialPort)
        result.push(
          new CopyWebpackPlugin({
            patterns: [
              {
                from: path.resolve(serialPort, './bindings-cpp/prebuilds'),
                to: path.resolve(
                  this.dir,
                  './dist/native/serialport/prebuilds'
                ),
              },
            ],
          })
        );
    }
    if (this.destination == Destination.Analyze)
      result.push(
        new BundleAnalyzerPlugin() as unknown as WebpackPluginInstance
      );
    return [...result, ...plugins];
  }
  private buildOutput() {
    const { output = {} } = this.baseConfig;
    const p = path.resolve(this.dir, 'dist');
    return { path: p, publicPath: '/', ...output };
  }
  build() {
    return (env: Record<string, string>, argv: Record<string, string>) => {
      this.destination = ToDestination(
        env.DESTINATION || env.destination || process.env.DESTINATION
      );
      const {
        mode = ToMode(env.NODE_ENV || process.env.NODE_ENV),
        target,
        entry = {
          main: [path.join(this.dir, 'src/index.ts')],
        },
      } = this.baseConfig;
      Object.assign(this.config, {
        mode,
        target,
        entry,
      });

      const {
        devtool = this.isDevelopment()
          ? 'inline-source-map'
          : this.isProduction()
            ? undefined
            : undefined,
        devServer = this.hasWebTargets()
          ? {
            historyApiFallback: true,
            port: 9000,
          }
          : undefined,
      } = this.baseConfig;
      const inferred = { devtool, devServer };
      const built: Record<string, any> = {
        module: this.buildModule(),
        optimization: this.buildOptimization(),
        performance: this.buildPerformance(),
        resolve: this.buildResolve(),
        plugins: this.buildPlugins(),
        output: this.buildOutput(),
      };
      return Object.assign({}, this.baseConfig, inferred, this.config, built);
    };
  }
}

export function runWebpack(
  dir: string,
  env?: Record<string, string>,
  argv?: Record<string, string>
) {
  const config = new WebpackConfigBuilder(dir).build();
  const bundler = webpack(config(env || {}, argv || {}));
  bundler.run((err) => {
    if (err) {
      console.log('Webpack compiler encountered a fatal error.', err);
    }
  });
}
