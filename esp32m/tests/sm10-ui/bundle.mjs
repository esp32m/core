import assert from "node:assert/strict";
import { mkdtempSync, readdirSync, rmSync } from "node:fs";
import { createRequire } from "node:module";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, "../..");
const web = path.join(core, "web-ui");
const require = createRequire(path.join(web, "package.json"));
const webpack = require("webpack");
const { WebpackConfigBuilder } = require("@ts-libs/dev");
const temporaryRoot = path.resolve(os.tmpdir());
const output = mkdtempSync(path.join(temporaryRoot, "esp32m-sm10-ui-"));
const builder = new WebpackConfigBuilder(path.join(web, "esp32m/ui"), {
  context: web,
  mode: "production",
  entry: path.join(here, "bundle-entry.ts"),
  resolve: { modules: [path.join(web, "node_modules")] },
  resolveLoader: { modules: [path.join(web, "node_modules")] },
  output: {
    path: output,
    filename: "sm10.js",
    library: { type: "window", name: "SM10Smoke" },
  },
});
const config = builder.build()({}, {});
// The smoke entry is a plugin library, not a full application's HTML shell.
// Keep the project's production Babel/MUI/optimization settings unchanged.
config.plugins = config.plugins.filter(
  (plugin) =>
    !["HtmlWebpackPlugin", "CleanWebpackPlugin"].includes(
      plugin.constructor.name,
    ),
);

const originalDirectory = process.cwd();
try {
  process.chdir(web);
  const compiler = webpack(config);
  const stats = await new Promise((resolve, reject) =>
    compiler.run((error, result) => (error ? reject(error) : resolve(result))),
  );
  await new Promise((resolve, reject) =>
    compiler.close((error) => (error ? reject(error) : resolve())),
  );
  const summary = stats.toJson({
    all: false,
    errors: true,
    warnings: true,
    assets: true,
  });
  assert.equal(
    stats.hasErrors(),
    false,
    JSON.stringify(summary.errors, null, 2),
  );
  assert.equal(
    stats.hasWarnings(),
    false,
    JSON.stringify(summary.warnings, null, 2),
  );
  assert(readdirSync(output).includes("sm10.js"));
  console.log(
    `SM10 production UI bundle passed: ${summary.assets.find((asset) => asset.name === "sm10.js").size} bytes; no errors or warnings.`,
  );
} finally {
  process.chdir(originalDirectory);
  if (
    path.dirname(output) === temporaryRoot &&
    path.basename(output).startsWith("esp32m-sm10-ui-")
  )
    rmSync(output, { recursive: true, force: true });
}
