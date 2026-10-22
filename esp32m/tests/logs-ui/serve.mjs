import assert from "node:assert/strict";
import { mkdtempSync, readFileSync } from "node:fs";
import http from "node:http";
import { createRequire } from "node:module";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { makeFixture } from "./fixture.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const web = path.resolve(here, "../../web-ui");
const require = createRequire(path.join(web, "package.json"));
const webpack = require("webpack");
const { WebpackConfigBuilder } = require("@ts-libs/dev");
const output = mkdtempSync(path.join(os.tmpdir(), "esp32m-logs-preview-"));
const builder = new WebpackConfigBuilder(path.join(web, "esp32m/ui"), {
  context: web,
  mode: "development",
  entry: path.join(here, "preview.tsx"),
  resolve: { modules: [path.join(web, "node_modules")] },
  resolveLoader: { modules: [path.join(web, "node_modules")] },
  output: { path: output, filename: "preview.js" },
});
const config = builder.build()({}, {});
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
  assert.equal(
    stats.hasErrors(),
    false,
    stats.toString({ all: false, errors: true }),
  );
  assert.equal(
    stats.hasWarnings(),
    false,
    stats.toString({ all: false, warnings: true }),
  );
} finally {
  process.chdir(originalDirectory);
}

const javascript = readFileSync(path.join(output, "preview.js"));
const html =
  '<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>esp32m Logs UI fixture</title></head><body><div id="root"></div><script src="/preview.js"></script></body></html>';
const server = http.createServer((request, response) => {
  const url = new URL(request.url, "http://127.0.0.1");
  if (url.pathname === "/preview.js") {
    response.setHeader("Content-Type", "text/javascript; charset=utf-8");
    response.end(javascript);
  } else if (url.pathname === "/logs/download") {
    const fixture = makeFixture();
    const id = url.searchParams.get("id");
    if (
      id !== null &&
      !Object.prototype.hasOwnProperty.call(fixture.contents, id)
    ) {
      response.writeHead(404, { "Content-Type": "text/plain" });
      response.end("Log not found");
      return;
    }
    const text =
      id === null
        ? Object.entries(fixture.contents)
            .map(([name, body]) => `--- ${name} ---\n${body}`)
            .join("\n")
        : fixture.contents[id];
    response.writeHead(200, {
      "Content-Type": "text/plain; charset=utf-8",
      "Content-Disposition": 'attachment; filename="esp32m-logs-fixture.txt"',
      "Cache-Control": "no-store",
    });
    response.end(text);
  } else {
    response.setHeader("Content-Type", "text/html; charset=utf-8");
    response.end(html);
  }
});
const requestedPort = Number(process.argv[2] || 0);
await new Promise((resolve) =>
  server.listen(requestedPort, "127.0.0.1", resolve),
);
console.log(
  `Logs System fixture ready: http://127.0.0.1:${server.address().port}/`,
);
console.log(
  "Modes: ?mode=empty | unsupported | unavailable | stale | loss | legacy | lowspace; languages: ?lang=uk | de | it",
);
console.log(`Temporary compiled assets: ${output}`);
console.log(
  "Stop with Ctrl+C. This server accesses no device or external service.",
);
