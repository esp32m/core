import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import http from "node:http";
import { createRequire } from "node:module";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const web = path.resolve(here, "../../web-ui");
const require = createRequire(path.join(web, "package.json"));
const runtime =
  process.env.BFS_PLAYWRIGHT_ROOT ||
  path.join(
    os.homedir(),
    ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules",
  );
const { chromium } = require(path.join(runtime, "playwright"));
const webpack = require("webpack");
const { WebpackConfigBuilder } = require("@ts-libs/dev");
const output = mkdtempSync(path.join(os.tmpdir(), "esp32m-bfs-browser-"));
const fixture = JSON.parse(
  readFileSync(path.join(here, "fixture.json"), "utf8"),
);
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
} finally {
  process.chdir(originalDirectory);
}
const html = `<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>BFS UI test fixture</title></head><body><div id="root"></div><script>window.bfsFixture=${JSON.stringify(fixture)};</script><script src="/preview.js"></script></body></html>`;
const javascript = readFileSync(path.join(output, "preview.js"));
const server = http.createServer((request, response) => {
  if (request.url === "/preview.js") {
    response.setHeader("Content-Type", "text/javascript");
    response.end(javascript);
  } else {
    response.setHeader("Content-Type", "text/html");
    response.end(html);
  }
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const url = `http://127.0.0.1:${server.address().port}`;
let browser;
const failures = [];
try {
  browser = await chromium.launch({ channel: "chrome", headless: true });
  const page = await browser.newPage();
  page.on("pageerror", (error) => failures.push(String(error)));
  const screenshot = async (filename, fullPage = false) => {
    await page.mouse.move(0, 0);
    await page.locator('[role="tooltip"]').waitFor({ state: "hidden" });
    await page.waitForFunction(() =>
      [...document.querySelectorAll('[role="dialog"]')].every((dialog) => {
        for (let node = dialog; node; node = node.parentElement)
          if (Number(getComputedStyle(node).opacity) < 0.99) return false;
        return true;
      }),
    );
    await page.screenshot({
      path: path.join(output, filename),
      fullPage,
      animations: "disabled",
    });
  };
  for (const viewport of [
    { width: 1280, height: 900 },
    { width: 390, height: 844 },
    { width: 320, height: 740 },
  ]) {
    const prefix = `${viewport.width}`;
    await page.setViewportSize(viewport);
    await page.goto(url);
    await page
      .getByRole("button", { name: "Configuration", exact: true })
      .waitFor();
    await screenshot(`${prefix}-card.png`, true);
    assert(
      await page.evaluate(
        () => document.documentElement.scrollWidth <= window.innerWidth,
      ),
      `${prefix}: card overflows viewport`,
    );
    await page.getByRole("button", { name: "Start", exact: true }).click();
    await page.getByRole("dialog").waitFor();
    await screenshot(`${prefix}-start.png`);
    await page.getByRole("button", { name: "cancel", exact: true }).click();
    await page
      .getByRole("button", { name: "Configuration", exact: true })
      .click();
    await page
      .getByRole("button", { name: "Edit F1-02", exact: true })
      .waitFor();
    assert.equal(
      await page.getByRole("button", { name: /^Edit F/ }).count(),
      79,
    );
    const tableFits = await page
      .getByRole("table", { name: "BFS parameters" })
      .evaluate((table) => table.scrollWidth <= table.clientWidth + 1);
    assert(tableFits, `${prefix}: parameter table overflow`);
    assert(await page.getByText('Cached', { exact: true }).count() > 0);
    await screenshot(`${prefix}-parameters.png`);
    await page.getByRole('button', { name: 'Edit 0x0017', exact: true }).click();
    await page.getByRole('textbox', { name: 'Value (bar)' }).fill('3.5');
    await screenshot(`${prefix}-pressure-edit.png`);
    await page.getByRole('button', { name: 'OK', exact: true }).click();
    await page.getByRole("button", { name: "Edit F1-02", exact: true }).click();
    await page.getByRole("textbox", { name: "Value (s)" }).fill("8.2");
    await screenshot(`${prefix}-edit.png`);
    await page.getByRole("button", { name: "OK", exact: true }).click();
    await page.getByRole("button", { name: "Edit F2-04", exact: true }).click();
    await page.getByRole("textbox", { name: "Value", exact: true }).fill("4");
    assert(await page.getByRole("button", { name: "OK", exact: true }).isDisabled());
    await page.getByRole("textbox", { name: "Value", exact: true }).fill("3");
    await screenshot(`${prefix}-phase-loss-edit.png`);
    await page.getByRole("button", { name: "OK", exact: true }).click();
    await page.getByRole("button", { name: "Close", exact: true }).click();
    await page.getByRole("button", { name: "Fault info", exact: true }).click();
    await page.getByText("Historical fault records").waitFor();
    await screenshot(`${prefix}-faults.png`);
    await page.getByRole("button", { name: "Close", exact: true }).click();
    await page
      .getByRole("button", { name: "Configure BFS pump", exact: true })
      .click();
    await page.getByRole("textbox", { name: "Modbus slave address" }).waitFor();
    await screenshot(`${prefix}-settings.png`);
  }
  assert.deepEqual(failures, []);
  writeFileSync(
    path.join(output, "result.json"),
    JSON.stringify(
      { passed: true, viewports: [1280, 390, 320], errors: failures },
      null,
      2,
    ),
  );
  console.log(
    `BFS browser verification passed at 1280, 390 and 320 px. Screenshots: ${output}`,
  );
} finally {
  await browser?.close();
  await new Promise((resolve) => server.close(resolve));
}
