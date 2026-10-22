import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { makeFixture } from "./fixture.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(path.resolve(here, "../../web-ui/package.json"));
const runtime =
  process.env.LOGS_PLAYWRIGHT_ROOT ||
  path.join(
    os.homedir(),
    ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules",
  );
const { chromium } = require(path.join(runtime, "playwright"));
const output = mkdtempSync(path.join(os.tmpdir(), "esp32m-logs-browser-"));
const url = process.argv[2] || "http://127.0.0.1:9191/";
const errors = [];
const results = [];
const browser = await chromium.launch({ channel: "chrome", headless: true });
try {
  const page = await browser.newPage();
  page.on("pageerror", (error) => errors.push(String(error)));
  page.on("console", (message) => {
    if (message.type() === "error") errors.push(message.text());
  });
  for (const viewport of [
    { width: 1280, height: 900 },
    { width: 390, height: 844 },
    { width: 320, height: 740 },
  ]) {
    await page.setViewportSize(viewport);
    await page.goto(url);
    await page.getByRole("button", { name: "Browse", exact: true }).waitFor();
    assert.equal(
      await page.getByRole("button", { name: "Refresh", exact: true }).count(),
      0,
      "Logs card has no manual refresh",
    );
    if (viewport.width === 1280) {
      const original = await page.evaluate(() => {
        const original = { ...window.logsFixture.fixture.state };
        window.logsFixture.pushState({
          quotaPercent: 60,
          usedBytes: 65536,
          enabled: true,
        });
        return original;
      });
      await page.getByText("60% of free space", { exact: true }).waitFor();
      await page.getByText("Recording", { exact: true }).waitFor();
      assert.equal(
        await page.evaluate(
          () =>
            window.logsFixture.requests.filter(
              (request) =>
                request.target === "logs" && request.command === "state-get",
            ).length,
        ),
        0,
      );
      await page.evaluate(
        (original) => window.logsFixture.pushState(original),
        original,
      );
      await page.getByText("50% of free space", { exact: true }).waitFor();
    }
    await page
      .getByRole("button", { name: "Browse", exact: true })
      .scrollIntoViewIfNeeded();
    await page.screenshot({
      path: path.join(output, `${viewport.width}-system.png`),
      fullPage: true,
    });
    const systemFits = await page.evaluate(
      () => document.documentElement.scrollWidth <= window.innerWidth,
    );
    await page.getByRole("button", { name: "Browse", exact: true }).click();
    const dialog = page.getByRole("dialog");
    await page.getByRole("button", { name: "Older", exact: true }).waitFor();
    await page.getByLabel("Log contents").waitFor();
    assert(
      await page.getByLabel("Log contents").textContent(),
      "Log text rendered",
    );
    assert(
      await dialog.evaluate((node) => node.scrollWidth <= node.clientWidth + 1),
      "Browser dialog must fit",
    );
    await page.screenshot({
      path: path.join(output, `${viewport.width}-browser.png`),
      animations: "disabled",
    });
    await page.getByRole("button", { name: "Older", exact: true }).click();
    await page.getByRole("button", { name: "Latest", exact: true }).click();
    if (viewport.width === 1280) {
      const downloaded = page.waitForEvent("download");
      await page
        .getByRole("link", { name: "Export file", exact: true })
        .click();
      const download = await downloaded;
      assert.equal(await download.failure(), null);
      const filename = path.join(output, "selected-export.txt");
      await download.saveAs(filename);
      assert.equal(
        readFileSync(filename, "utf8"),
        makeFixture().contents["m-log-0000000000000002.txt"],
      );
    }
    await page.getByRole("button", { name: "Close", exact: true }).click();
    await dialog.waitFor({ state: "hidden" });
    await page.getByRole("button", { name: "Settings", exact: true }).click();
    await page.getByText("Quota range: 1–100%", { exact: true }).waitFor();
    assert.equal(
      await page.getByLabel("Storage quota", { exact: true }).inputValue(),
      "50",
    );
    await page.getByLabel("Storage quota", { exact: true }).fill("25");
    await page
      .getByText(
        "Saving a smaller quota or shorter age may permanently remove older logs.",
        { exact: true },
      )
      .waitFor();
    assert(
      await dialog.evaluate((node) => node.scrollWidth <= node.clientWidth + 1),
      "Settings dialog must fit",
    );
    await page.screenshot({
      path: path.join(output, `${viewport.width}-settings.png`),
      animations: "disabled",
    });
    if (viewport.width < 600) {
      await page
        .getByText(
          "Saving a smaller quota or shorter age may permanently remove older logs.",
          { exact: true },
        )
        .scrollIntoViewIfNeeded();
      await page.screenshot({
        path: path.join(output, `${viewport.width}-settings-bottom.png`),
        animations: "disabled",
      });
    }
    await page.getByRole("button", { name: "Cancel", exact: true }).click();
    if (viewport.width === 1280) {
      // Cancel retains 50%; saving 25% must reopen as a percentage rather than
      // silently turning the resolved byte value into a fixed quota.
      await page.getByRole("button", { name: "Settings", exact: true }).click();
      assert.equal(
        await page.getByLabel("Storage quota", { exact: true }).inputValue(),
        "50",
      );
      await page.getByLabel("Storage quota", { exact: true }).fill("25");
      await page.getByRole("button", { name: "OK", exact: true }).click();
      await dialog.waitFor({ state: "hidden" });
      await page.getByRole("button", { name: "Settings", exact: true }).click();
      assert.equal(
        await page.getByLabel("Storage quota", { exact: true }).inputValue(),
        "25",
      );
      const unit = page.getByRole("combobox", { name: "Unit", exact: true });
      assert((await unit.textContent()).includes("% of free space"));
      await unit.click();
      await page.getByRole("option", { name: "KiB", exact: true }).click();
      assert.equal(
        Number(
          await page.getByLabel("Storage quota", { exact: true }).inputValue(),
        ) * 1024,
        Math.floor((makeFixture().info.quotaBaseBytes * 25) / 100),
      );
      await page.getByRole("button", { name: "Cancel", exact: true }).click();
    }
    results.push({ ...viewport, systemFits });
  }
  for (const [mode, message] of [
    ["empty", "No stored logs."],
    ["unsupported", "This firmware does not support stored logs."],
    ["unavailable", "Log storage is unavailable on this device."],
    [
      "loss",
      "Some log records were dropped or truncated. Stored history is incomplete.",
    ],
  ]) {
    await page.goto(`${url}?mode=${mode}`);
    if (mode === "empty")
      await page.getByRole("button", { name: "Browse", exact: true }).click();
    await page.getByText(message, { exact: true }).waitFor();
  }
  await page.goto(`${url}?mode=legacy`);
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  assert.equal(
    await page.getByLabel("Storage quota", { exact: true }).inputValue(),
    "256",
  );
  await page.getByRole("combobox", { name: "Unit", exact: true }).click();
  assert.equal(
    await page
      .getByRole("option", { name: "% of free space", exact: true })
      .count(),
    0,
  );
  await page.keyboard.press("Escape");
  await page.getByRole("button", { name: "Cancel", exact: true }).click();
  await page.goto(`${url}?mode=lowspace`);
  await page.getByRole("button", { name: "Settings", exact: true }).click();
  await page
    .getByText(
      "Not enough space for the minimum log quota after the filesystem reserve.",
      { exact: true },
    )
    .waitFor();
  assert(
    await page.getByRole("button", { name: "OK", exact: true }).isEnabled(),
    "A disabled percentage policy remains saveable",
  );
  await page.getByLabel("Store logs on device", { exact: true }).check();
  assert(
    await page.getByRole("button", { name: "OK", exact: true }).isDisabled(),
    "Cannot enable capture below the minimum effective quota",
  );
  assert.deepEqual(errors, []);
  writeFileSync(
    path.join(output, "result.json"),
    JSON.stringify({ results, errors }, null, 2),
  );
  console.log(JSON.stringify({ output, results, errors }, null, 2));
  assert(
    results.every((result) => result.systemFits),
    "System cards overflow a viewport; see screenshots",
  );
} finally {
  await browser.close();
}
