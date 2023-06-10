import path from 'path';
import TerserPlugin from 'terser-webpack-plugin';
import HtmlWebpackPlugin from 'html-webpack-plugin';
import { CleanWebpackPlugin } from 'clean-webpack-plugin';
import CompressionPlugin from 'compression-webpack-plugin';
import { BundleAnalyzerPlugin } from 'webpack-bundle-analyzer';
import { PackageJson } from 'types-package-json';
//import WebpackStringReplacer from 'webpack-string-replacer';

import {
  ModuleOptions,
  RuleSetRule,
  Configuration,
  DefinePlugin,
  webpack,
  optimize,
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
  private buildBabelLoader() {
    let targets: any;
    if (this.hasNodeTarget()) targets = { node: true };
    else if (this.hasWebTargets()) targets = { browsers: '> 5%' };
    const presets: Array<any> = [
      [
        '@babel/env',
        {
          modules: false,
          targets,
        },
      ],
      ['@babel/typescript', { optimizeConstEnums: true }],
    ];
    if (this.package.dependencies?.['react'])
      presets.push(['@babel/react', { runtime: 'automatic' }]);
    const plugins: Array<any> = [
      ['@babel/plugin-proposal-decorators', { version: 'legacy' }],
    ];
    if (this.package.dependencies?.['lodash']) plugins.push('lodash');
    const rule: RuleSetRule = {
      loader: 'babel-loader',
      options: {
        presets,
        plugins,
      },
    };
    return {
      test: /\.tsx?$/,
      exclude: /node_modules/,
      use: [rule],
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
    built.push(bindingsFixLoader);
    const url = findModuleRule(rules, 'url-loader');
    if (!url && this.hasWebTargets()) built.push(inlineImageLoader);
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
    const limit = 8 * 1024 * 1024;
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
      /*result.push(
        new WebpackStringReplacer({
          rules: [
            {
              fileInclude: /bindings\.js/,
              applyStage: 'loader',
              replacements: [
                {
                  pattern: 'opts.module_root = ',
                  replacement: 'opts.module_root = __dirname; //',
                },
              ],
            },
          ],
        })
      );*/
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
      const built = {
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
