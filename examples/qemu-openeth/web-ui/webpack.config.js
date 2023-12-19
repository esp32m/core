const path = require('path');
const { WebpackConfigBuilder } = require('@ts-libs/dev');
const isDevServer = process.argv.some(i=>i.includes('node-env=development'));
const builder = new WebpackConfigBuilder(
  __dirname,
  {
    resolve: { modules: [path.resolve(__dirname, './node_modules')] },
  },
  isDevServer ? { defines: { esp32m: { backend: { host: 'qemu-openeth.local' } } } }:{}
);
module.exports = builder.build();
