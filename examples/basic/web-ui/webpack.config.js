const packageJson=require("./package.json");
const path = require('path');
const webpack = require('webpack');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const { CleanWebpackPlugin } = require('clean-webpack-plugin');
const TerserPlugin = require('terser-webpack-plugin');
const CompressionPlugin = require('compression-webpack-plugin');
const BundleAnalyzerPlugin = require('webpack-bundle-analyzer').BundleAnalyzerPlugin;

const nodeEnv = process.env.NODE_ENV || "development";
const isdev = nodeEnv == "development";
const mode = isdev ? "development" : "production";

const sharedPlugins = [
    new webpack.DefinePlugin({
      "window.__build_info": {
        version: JSON.stringify(packageJson.version),
        built: JSON.stringify((new Date()).toISOString()),
        mode: JSON.stringify(mode)
      },
    }),
    new HtmlWebpackPlugin({
      title: 'app',
      template: 'src/index.html'
    }),
];

const pluginsMap = {
 development:[...sharedPlugins],
 production: [...sharedPlugins, 
    new CleanWebpackPlugin({}),
    new CompressionPlugin({
      test: /\.(js|css|html|svg)$/,
//      filename: '[path].br[query]',
//      algorithm: 'brotliCompress',
      algorithm: 'gzip',
    }),
    ...(nodeEnv == "analyze"?[new BundleAnalyzerPlugin()]:[]),
 ]
}

module.exports = {
  mode,
  entry: {
    main: ["./src/index.ts"]
  },
  devtool: isdev ? "source-map" : "hidden-source-map",
  devServer: {
    contentBase: "./dist",
    historyApiFallback: true,
    hot: true
  },
  module: {
    rules: [
      {
        test: /\.(j|t)sx?$/,
        exclude: /node_modules/,
        use: [ "babel-loader", "ts-loader"]
      }
    ]
  },
  optimization: {
    minimize: !isdev,
    minimizer: [new TerserPlugin({ parallel: true, terserOptions: { output: { comments: false } } })]
  },
  performance: {
    maxEntrypointSize: 8000000,
    maxAssetSize: 8000000
  },
  resolve: {
    extensions: ['.tsx', '.ts', '.js', '.jsx'],
    alias:{
      react: path.resolve('./node_modules/react'),
      "@material-ui": path.resolve('./node_modules/@material-ui'),
      "@babel": path.resolve('./node_modules/@babel'),
    }
  },
  plugins: pluginsMap[mode],
  output: {
    path: path.resolve(__dirname, 'dist'),
  },
};