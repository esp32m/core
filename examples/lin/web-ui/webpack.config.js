const path = require('path');
const { WebpackConfigBuilder } = require('@ts-libs/dev');
const isDevServer = process.argv.some(i=>i.includes('node-env=development'));
const builder = new WebpackConfigBuilder(
  __dirname,
  {
    resolve: { modules: [path.resolve(__dirname, './node_modules')] },
    devServer: { port:9091, historyApiFallback: true },
  },
  isDevServer ? { defines: { esp32m: { backend: { host: 'lin.local' } } } }:{}
);
module.exports = builder.build();
