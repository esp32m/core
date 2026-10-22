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
  process.env.HSMWT10_PLAYWRIGHT_ROOT ||
  path.join(
    os.homedir(),
    ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules",
  );
const { chromium } = require(path.join(runtime, "playwright"));
const webpack = require("webpack");
const { WebpackConfigBuilder } = require("@ts-libs/dev");
const output = mkdtempSync(path.join(os.tmpdir(), "esp32m-hsmwt10-browser-"));
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
  let stats;
  try {
    stats = await new Promise((resolve, reject) =>
      compiler.run((error, result) => (error ? reject(error) : resolve(result))),
    );
  } finally {
    await new Promise((resolve, reject) =>
      compiler.close((error) => (error ? reject(error) : resolve())),
    );
  }
  assert.equal(
    stats.hasErrors(),
    false,
    stats.toString({ all: false, errors: true }),
  );
} finally {
  process.chdir(originalDirectory);
}
const html = `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>HSM-WT10 offline UI test</title></head><body><div id="root"></div><script src="/preview.js"></script></body></html>`;
const javascript = readFileSync(path.join(output, "preview.js"));
const server = http.createServer((request, response) => {
  if (request.url === "/preview.js") {
    response.setHeader("Content-Type", "text/javascript; charset=utf-8");
    response.end(javascript);
  } else {
    response.setHeader("Content-Type", "text/html; charset=utf-8");
    response.end(html);
  }
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const url = `http://127.0.0.1:${server.address().port}`;
let browser;
const failures = [];
const screenshots = [];
try {
  browser = await chromium.launch({ channel: "chrome", headless: true });
  const page = await browser.newPage();
  page.on("pageerror", (error) => failures.push(String(error)));
  // This harness has no physical/backend endpoint. Refuse external requests.
  await page.route("**/*", (route) => {
    if (route.request().url().startsWith(`${url}/`)) return route.continue();
    failures.push(`External request blocked: ${route.request().url()}`);
    return route.abort();
  });
  const screenshot = async (filename) => {
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
      fullPage: true,
      animations: "disabled",
    });
    screenshots.push(filename);
  };
  const fits = async (label) => {
    assert(
      await page.evaluate(
        () => document.documentElement.scrollWidth <= window.innerWidth,
      ),
      `${label}: page overflows viewport`,
    );
    for (const dialog of await page.getByRole("dialog").all()) {
      assert(
        await dialog.evaluate((element) => {
          const box = element.getBoundingClientRect();
          return (
            box.left >= -1 &&
            box.right <= innerWidth + 1 &&
            element.scrollWidth <= element.clientWidth + 1
          );
        }),
        `${label}: settings overflow viewport`,
      );
    }
  };
  const openSettings = async () => {
    await page.getByRole("button", { name: "Configure HSM-WT10" }).click();
    await page.getByLabel("Modbus slave address", { exact: true }).waitFor();
    await page.getByRole("button", { name: "Refresh", exact: true }).waitFor();
    await page.waitForFunction(() => {
      const refresh = [...document.querySelectorAll('button')].find((button) => button.textContent === 'Refresh');
      return refresh && !refresh.disabled;
    });
  };
  const closeSettings = async () => {
    await page.getByRole("button", { name: "Cancel", exact: true }).click();
    await page.getByRole("dialog").waitFor({ state: "hidden" });
  };
  const calls = () => page.evaluate(() => window.hsmwt10Test.calls);

  for (const viewport of [
    { width: 1280, height: 900 },
    { width: 390, height: 844 },
    { width: 320, height: 740 },
  ]) {
    const prefix = `${viewport.width}`;
    await page.setViewportSize(viewport);
    await page.goto(url);
    await page.getByRole("button", { name: "Configure HSM-WT10" }).waitFor();
    await page.getByText("Waiting for sensor data", { exact: true }).waitFor();
    assert.equal(await page.getByText("No leak detected", { exact: true }).count(), 0);
    await page.evaluate(() => window.hsmwt10Test.startTelemetry());
    await page.getByText("No leak detected", { exact: true }).waitFor();
    await screenshot(`${prefix}-normal.png`);
    await fits(`${prefix} normal`);

    for (const scenario of ["leak", "stale", "offline", "pending"]) {
      await page.evaluate((name) => window.hsmwt10Test.setScenario(name), scenario);
      await page.getByText(
        scenario === "leak" ? "Leak detected" : "No recent sensor reading",
        { exact: true },
      ).waitFor();
      if (scenario !== "leak")
        await page.getByText("Unavailable", { exact: true }).waitFor();
      if (scenario === "pending")
        await page.getByText(/Communication change is awaiting verification/).waitFor();
      await screenshot(`${prefix}-${scenario}.png`);
      await fits(`${prefix} ${scenario}`);
    }

    await openSettings();
    assert(await page.getByRole("button", { name: "Save", exact: true }).isDisabled());
    await screenshot(`${prefix}-pending-settings.png`);
    await fits(`${prefix} pending settings`);
    await closeSettings();
    await page.evaluate(() => window.hsmwt10Test.setScenario("normal"));
    await openSettings();
    assert.equal(await page.getByLabel("Modbus slave address", { exact: true }).inputValue(), "5");
    assert.equal(await page.getByLabel("Sensitivity", { exact: true }).inputValue(), "300");
    assert.equal(await page.getByLabel("Alarm delay (s)", { exact: true }).inputValue(), "0");
    assert(await page.getByRole("button", { name: "Save", exact: true }).isDisabled());
    await screenshot(`${prefix}-settings.png`);
    await fits(`${prefix} settings`);
    await page.getByText("200–500; higher values are more sensitive", { exact: true }).waitFor();

    for (const [label, invalid, original] of [
      ["Modbus slave address", "0", "5"],
      ["Modbus slave address", "248", "5"],
      ["Modbus slave address", "1.5", "5"],
      ["Sensitivity", "199", "300"],
      ["Sensitivity", "501", "300"],
      ["Alarm delay (s)", "-1", "0"],
      ["Alarm delay (s)", "65536", "0"],
      ["Alarm delay (s)", "1.5", "0"],
    ]) {
      await page.getByLabel(label, { exact: true }).fill(invalid);
      assert(
        await page.getByRole("button", { name: "Save", exact: true }).isDisabled(),
        `${prefix}: accepts invalid ${label}=${invalid}`,
      );
      await page.getByLabel(label, { exact: true }).fill(original);
    }

    await page.getByLabel("Baud rate", { exact: true }).click();
    assert.deepEqual(
      (await page.getByRole("option").allTextContents()).map(Number),
      [2400, 4800, 9600, 19200, 38400, 115200],
    );
    await page.getByRole("option", { name: "9600", exact: true }).click();
    const beforeRefresh = (await calls()).length;
    await page.getByRole("button", { name: "Refresh", exact: true }).click();
    await page.waitForFunction(
      (count) => window.hsmwt10Test.calls.length > count,
      beforeRefresh,
    );
    assert(
      (await calls()).slice(beforeRefresh).every((call) => call.command !== "config-set"),
      "Refresh sent a configuration write",
    );

    await page.getByLabel("Sensitivity", { exact: true }).fill("350");
    await page.evaluate(() => window.hsmwt10Test.setWriteDelay(500));
    const beforeSave = (await calls()).length;
    const save = page.getByRole("button", { name: "Save", exact: true });
    await save.evaluate((button) => { button.click(); button.click(); });
    await page.waitForFunction(() =>
      window.hsmwt10Test.getConfig().sensitivity === 350,
    );
    await page.waitForFunction(
      (count) => window.hsmwt10Test.calls.slice(count).some((call) => call.command === "state-get"),
      beforeSave,
    );
    const savedCalls = (await calls()).slice(beforeSave);
    assert.deepEqual(
      savedCalls.filter((call) => call.command === "config-set"),
      [{ target: "HSM-WT10", command: "config-set", data: { sensitivity: 350 } }],
      `${prefix}: Save must send changed fields exactly once`,
    );
    const setIndex = savedCalls.findIndex((call) => call.command === "config-set");
    assert(savedCalls.slice(setIndex + 1).some((call) => call.command === "config-get"));
    assert(savedCalls.slice(setIndex + 1).some((call) => call.command === "state-get"));
    if (await page.getByRole("dialog").isVisible()) await closeSettings();
    await openSettings();
    assert.equal(await page.getByLabel("Sensitivity", { exact: true }).inputValue(), "350");
    await closeSettings();

    await page.evaluate(() => window.hsmwt10Test.setConfigReadError(263));
    await openSettings();
    assert(await page.getByRole("button", { name: "Save", exact: true }).isDisabled());
    await screenshot(`${prefix}-config-read-error.png`);
    await fits(`${prefix} config read error`);
    await closeSettings();
  }

  // Stable module selection keeps the card visible after its Modbus ID changes.
  await page.setViewportSize({ width: 1280, height: 900 });
  await page.goto(url);
  await page.getByText("Waiting for sensor data", { exact: true }).waitFor();
  await page.evaluate(() => window.hsmwt10Test.startTelemetry());
  await page.getByText("No leak detected", { exact: true }).waitFor();
  await openSettings();
  await page.getByLabel("Modbus slave address", { exact: true }).fill("7");
  await page.getByRole("button", { name: "Save", exact: true }).click();
  await page.getByRole("dialog").waitFor({ state: "hidden" });
  await page.getByRole("button", { name: "Configure HSM-WT10" }).waitFor();
  await page.getByText("7", { exact: true }).waitFor();
  assert.deepEqual(
    (await calls()).filter((call) => call.command === "config-set"),
    [{ target: "HSM-WT10", command: "config-set", data: { address: 7 } }],
  );
  await screenshot("1280-address-migrated.png");
  await page.evaluate(() => window.hsmwt10Test.stopTelemetry());
  await page.getByText("No recent sensor reading", { exact: true }).waitFor({ timeout: 7000 });
  await page.getByText("Unavailable", { exact: true }).waitFor();
  assert.equal(await page.getByText("No leak detected", { exact: true }).count(), 0);
  await screenshot("1280-stopped-updates.png");
  assert.deepEqual(failures, []);
  writeFileSync(
    path.join(output, "result.json"),
    JSON.stringify({ passed: true, viewports: [1280, 390, 320], errors: failures, screenshots }, null, 2),
  );
  console.log(`HSM-WT10 offline browser verification passed at 1280, 390 and 320 px. Screenshots: ${output}`);
} finally {
  await browser?.close();
  await new Promise((resolve) => server.close(resolve));
}
