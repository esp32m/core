// Deterministic local UI data, shared by the preview and download test server.
export function makeFixture(mode = "") {
  const time = Date.UTC(2026, 9, 21, 9, 0);
  const records = (count, boot) =>
    Array.from(
      { length: count },
      (_, index) =>
        `${new Date(time + index * 1000).toISOString()} I  [system] boot=${boot} record=${index.toString().padStart(4, "0")} Heap=187624 B · Журнал пристрою\n`,
    ).join("");
  const contents = {
    "m-log-0000000000000001.txt": records(80, "fixture-previous"),
    "m-log-0000000000000002.txt": records(230, "fixture-current"),
  };
  const files =
    mode === "empty"
      ? []
      : Object.entries(contents).map(([id, text], index) => ({
          id,
          size: new TextEncoder().encode(text).length,
          active: index === 1,
          bootId: index === 1 ? "fixture-current" : "fixture-previous",
          firstTime: index === 1 ? time : null,
          lastTime: index === 1 ? time + 229000 : null,
        }));
  const usedBytes = files.reduce((sum, file) => sum + file.size, 0);
  const freeBytes = mode === "lowspace" ? 4096 : 3900000;
  const quotaBaseBytes = freeBytes + usedBytes;
  const config =
    mode === "legacy"
      ? { enabled: false, quotaBytes: 262144, maxAgeDays: 7 }
      : {
          enabled: false,
          quotaBytes: Math.floor((quotaBaseBytes * 50) / 100),
          quotaPercent: 50,
          maxAgeDays: 7,
        };
  return {
    mode,
    contents,
    files,
    info: {
      schema: 1,
      ...(mode === "legacy"
        ? {}
        : { supportsQuotaPercent: true, quotaBaseBytes }),
      available: mode !== "unavailable",
      filesystem: "littlefs",
      label: "storage",
      totalBytes: 4194304,
      freeBytes,
      reserveBytes: 65536,
      minQuotaBytes: 8192,
      maxQuotaBytes: 4128768,
      pageBytes: 4096,
      maxPageBytes: 8192,
    },
    config,
    state: {
      available: mode !== "unavailable",
      enabled: false,
      ready: mode !== "unavailable",
      usedBytes,
      quotaBytes: config.quotaBytes,
      ...(mode === "legacy"
        ? {}
        : { quotaPercent: config.quotaPercent, maxAgeDays: config.maxAgeDays }),
      effectiveQuotaBytes: Math.max(
        0,
        Math.min(config.quotaBytes, quotaBaseBytes - 65536),
      ),
      freeBytes,
      ...(mode === "legacy" ? {} : { quotaBaseBytes }),
      reserveBytes: 65536,
      fileCount: files.length,
      dropped: mode === "loss" ? 17 : 0,
      truncated: mode === "loss" ? 2 : 0,
      pendingBytes: 0,
      timeValid: false,
      lastError:
        mode === "loss"
          ? "Fixture: capture paused because the filesystem reserve could not be maintained."
          : "",
    },
    app: {
      name: "Logs visual fixture",
      version: "fixture",
      built: "2026-10-21 09:00 UTC",
      sdk: "6.1",
      size: 2097152,
      time,
      uptime: 3600000,
    },
    hardware: {
      heap: { size: 327680, free: 187624, min: 170000, max: 160000 },
      chip: { model: 9, revision: 1, cores: 2, features: 18, freq: 240, rr: 1 },
      flash: { size: 16, speed: 80, mode: 0 },
    },
  };
}
