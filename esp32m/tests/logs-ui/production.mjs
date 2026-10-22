import assert from "node:assert/strict";
import { mkdtempSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const core = path.resolve(here, "../..");
const web = path.join(core, "web-ui");
const project = path.resolve(core, "../../projects/water-well/web-ui");
const require = createRequire(path.join(web, "package.json"));
const webpack = require("webpack");
const { WebpackConfigBuilder } = require("@ts-libs/dev");
const output = mkdtempSync(path.join(os.tmpdir(), "esp32m-logs-waterwell-"));
const builder = new WebpackConfigBuilder(project, {
  context: project,
  mode: "production",
  resolve: { modules: [path.join(web, "node_modules")] },
  resolveLoader: { modules: [path.join(web, "node_modules")] },
  output: { path: output },
});
const config = builder.build()({}, {});
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
  writeFileSync(
    path.join(output, "build-result.json"),
    JSON.stringify(summary, null, 2),
  );
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
  console.log(
    `Water-well production UI passed: ${summary.assets.find((asset) => asset.name === "main.js").size} bytes; no errors or warnings.`,
  );
  console.log(`Output (temporary only): ${output}`);
} finally {
  process.chdir(originalDirectory);
}
