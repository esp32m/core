"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.runWebpack = exports.WebpackConfigBuilder = void 0;
const path_1 = __importDefault(require("path"));
const terser_webpack_plugin_1 = __importDefault(require("terser-webpack-plugin"));
const html_webpack_plugin_1 = __importDefault(require("html-webpack-plugin"));
const clean_webpack_plugin_1 = require("clean-webpack-plugin");
const compression_webpack_plugin_1 = __importDefault(require("compression-webpack-plugin"));
const webpack_bundle_analyzer_1 = require("webpack-bundle-analyzer");
//import WebpackStringReplacer from 'webpack-string-replacer';
const webpack_1 = require("webpack");
require("webpack-dev-server");
var Destination;
(function (Destination) {
    Destination["Local"] = "local";
    Destination["Remote"] = "remote";
    Destination["Analyze"] = "analyze";
})(Destination || (Destination = {}));
function ToDestination(v) {
    if (!v)
        return;
    const a = [Destination.Local, Destination.Remote, Destination.Analyze];
    const i = a.indexOf(v);
    if (i >= 0)
        return a[i];
}
var Mode;
(function (Mode) {
    Mode["None"] = "none";
    Mode["Development"] = "development";
    Mode["Production"] = "production";
})(Mode || (Mode = {}));
function ToMode(v) {
    if (!v)
        return;
    const a = [Mode.None, Mode.Development, Mode.Production];
    const i = a.indexOf(v);
    if (i >= 0)
        return a[i];
}
function findModuleRule(rules, loader) {
    return Array.isArray(rules)
        ? rules.find((r) => r != '...' &&
            (r.loader == loader ||
                (Array.isArray(r.use) &&
                    r.use.some((u) => u == loader || u['loader'] == loader))))
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
const ipfsUtilsHttpFixLoader = (node) => ({
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
class WebpackConfigBuilder {
    builderConfig;
    baseConfig;
    config = {};
    dir;
    package;
    destination;
    constructor(dir, config, builderConfig) {
        this.builderConfig = builderConfig;
        this.dir = dir;
        this.baseConfig = Object.assign({}, config);
        this.package = require(path_1.default.join(dir, 'package.json'));
    }
    setTarget(target) {
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
        return (target == 'node' || (Array.isArray(target) && target.includes('node')));
    }
    buildBabelLoader() {
        let targets;
        if (this.hasNodeTarget())
            targets = { node: true };
        else if (this.hasWebTargets())
            targets = { browsers: '> 5%' };
        const presets = [
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
        const plugins = [
            ['@babel/plugin-proposal-decorators', { version: 'legacy' }],
        ];
        if (this.package.dependencies?.['lodash'])
            plugins.push('lodash');
        const rule = {
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
    buildModule() {
        const { module = {} } = this.baseConfig;
        const { rules = [], ...rest } = module;
        const built = [];
        const babel = findModuleRule(rules, 'babel-loader');
        if (!babel)
            built.push(this.buildBabelLoader());
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
        if (!url && this.hasWebTargets())
            built.push(inlineImageLoader);
        return { rules: [...rules, ...built], ...rest };
    }
    buildOptimization() {
        const { optimization = {} } = this.baseConfig;
        const minimize = this.isProduction();
        const minimizer = [
            new terser_webpack_plugin_1.default({
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
    buildPerformance() {
        const { performance = {} } = this.baseConfig;
        const limit = 8 * 1024 * 1024;
        return { maxEntrypointSize: limit, maxAssetSize: limit, ...performance };
    }
    buildResolve() {
        const { resolve = {} } = this.baseConfig;
        const alias = this.hasNodeTarget()
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
    buildInfoPlugin() {
        const globalObjectName = this.hasNodeTarget() ? 'global' : 'window';
        return new webpack_1.DefinePlugin({
            [`${globalObjectName}.__build_info`]: {
                version: JSON.stringify(this.package.version),
                built: JSON.stringify(new Date().toISOString()),
                mode: JSON.stringify(this.config.mode),
                destination: JSON.stringify(this.destination),
            },
            ...Object.fromEntries(Object.entries(this.builderConfig?.defines || {}).map(([k, v]) => [
                `${globalObjectName}.${k}`,
                JSON.stringify(v),
            ])),
        });
    }
    buildPlugins() {
        const { plugins = [] } = this.baseConfig;
        const buildInfo = this.buildInfoPlugin();
        const result = [buildInfo];
        if (this.hasWebTargets()) {
            result.push(new html_webpack_plugin_1.default({
                title: 'app',
                template: 'src/index.html',
            }));
            if (this.isProduction())
                result.push(new clean_webpack_plugin_1.CleanWebpackPlugin({}), new compression_webpack_plugin_1.default({
                    test: /\.(js|css|html|svg)$/,
                    algorithm: 'gzip',
                }));
        }
        if (this.hasNodeTarget()) {
            result.push(new webpack_1.optimize.LimitChunkCountPlugin({
                maxChunks: 1,
            }));
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
            result.push(new webpack_bundle_analyzer_1.BundleAnalyzerPlugin());
        return [...result, ...plugins];
    }
    buildOutput() {
        const { output = {} } = this.baseConfig;
        const p = path_1.default.resolve(this.dir, 'dist');
        return { path: p, publicPath: '/', ...output };
    }
    build() {
        return (env, argv) => {
            this.destination = ToDestination(env.DESTINATION || env.destination || process.env.DESTINATION);
            const { mode = ToMode(env.NODE_ENV || process.env.NODE_ENV), target, entry = {
                main: [path_1.default.join(this.dir, 'src/index.ts')],
            }, } = this.baseConfig;
            Object.assign(this.config, {
                mode,
                target,
                entry,
            });
            const { devtool = this.isDevelopment()
                ? 'eval-source-map'
                : this.isProduction()
                    ? 'source-map'
                    : undefined, devServer = this.hasWebTargets()
                ? {
                    historyApiFallback: true,
                    port: 9000,
                }
                : undefined, } = this.baseConfig;
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
exports.WebpackConfigBuilder = WebpackConfigBuilder;
function runWebpack(dir, env, argv) {
    const config = new WebpackConfigBuilder(dir).build();
    const bundler = (0, webpack_1.webpack)(config(env || {}, argv || {}));
    bundler.run((err) => {
        if (err) {
            console.log('Webpack compiler encountered a fatal error.', err);
        }
    });
}
exports.runWebpack = runWebpack;
//# sourceMappingURL=webpack.js.map