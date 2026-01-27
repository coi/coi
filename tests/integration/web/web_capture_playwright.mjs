#!/usr/bin/env node
import fs from "node:fs/promises";
import path from "node:path";

function parseSize(s) {
  const m = String(s || "").trim().match(/^(\d+)x(\d+)$/);
  if (!m) throw new Error(`invalid --size '${s}' (expected WxH, e.g. 960x540)`);
  return { width: parseInt(m[1], 10), height: parseInt(m[2], 10) };
}

function parseArgs(argv) {
  const out = { url: "", out: "", size: "960x540", timeoutMs: 12000, browserPath: "" };
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--url") out.url = argv[++i] || "";
    else if (a === "--out") out.out = argv[++i] || "";
    else if (a === "--size") out.size = argv[++i] || "";
    else if (a === "--timeout-ms") out.timeoutMs = parseInt(argv[++i] || "0", 10) || out.timeoutMs;
    else if (a === "--browser") out.browserPath = argv[++i] || "";
    else throw new Error(`Unknown argument: ${a}`);
  }
  if (!out.url || !out.out) {
    throw new Error("usage: web_capture_playwright.mjs --url <url> --out <png> [--size WxH] [--timeout-ms N] [--browser <path>]");
  }
  return out;
}

async function main() {
  const { url, out, size, timeoutMs, browserPath } = parseArgs(process.argv);
  const { width, height } = parseSize(size);

  const { chromium } = await import("playwright-core");
  const browser = await chromium.launch({
    headless: true,
    executablePath: browserPath || undefined,
    args: ["--no-sandbox", "--disable-dev-shm-usage", "--hide-scrollbars", `--window-size=${width},${height}`],
  });
  const ctx = await browser.newContext({ viewport: { width, height }, deviceScaleFactor: 1 });
  const page = await ctx.newPage();
  page.setDefaultTimeout(timeoutMs);

  await page.goto(url, { waitUntil: "load" });

  // Wait for COI app to mount and paint something.
  await page.waitForFunction(() => {
    if (document.querySelector(".root")) return true;
    const kids = document.body ? document.body.children : [];
    for (const el of kids) {
      if (el && el.tagName && el.tagName.toUpperCase() !== "SCRIPT") return true;
    }
    return false;
  });

  // One extra frame for the final DOM/paint commit.
  await page.evaluate(
    () =>
      new Promise((resolve) => {
        requestAnimationFrame(() => requestAnimationFrame(resolve));
      }),
  );

  await fs.mkdir(path.dirname(out), { recursive: true });
  await page.screenshot({ path: out });

  await page.close();
  await ctx.close();
  await browser.close();
}

await main().catch((err) => {
  console.error(err?.stack || String(err));
  process.exit(1);
});

