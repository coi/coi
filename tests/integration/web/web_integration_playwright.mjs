#!/usr/bin/env node
import fs from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

function parseSize(s) {
  const m = String(s || "").trim().match(/^(\d+)x(\d+)$/);
  if (!m) throw new Error(`invalid --size '${s}' (expected WxH, e.g. 960x540)`);
  return { width: parseInt(m[1], 10), height: parseInt(m[2], 10) };
}

function parseArgs(argv) {
  const out = {
    url: "",
    test: "",
    size: "960x540",
    timeoutMs: 12000,
    browserPath: "",
    headed: false,
    screenshot: "",
  };
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--url") out.url = argv[++i] || "";
    else if (a === "--test") out.test = argv[++i] || "";
    else if (a === "--size") out.size = argv[++i] || "";
    else if (a === "--timeout-ms") out.timeoutMs = parseInt(argv[++i] || "0", 10) || out.timeoutMs;
    else if (a === "--browser") out.browserPath = argv[++i] || "";
    else if (a === "--headed") out.headed = true;
    else if (a === "--screenshot") out.screenshot = argv[++i] || "";
    else throw new Error(`Unknown argument: ${a}`);
  }
  if (!out.url) {
    throw new Error(
      "usage: web_integration_playwright.mjs --url <url> [--test <file.mjs>] [--size WxH] [--timeout-ms N] [--browser <path>] [--headed] [--screenshot <png>]",
    );
  }
  return out;
}

async function waitForCoiMount(page) {
  await page.waitForFunction(() => {
    if (document.querySelector(".root")) return true;
    const kids = document.body ? document.body.children : [];
    for (const el of kids) {
      if (el && el.tagName && el.tagName.toUpperCase() !== "SCRIPT") return true;
    }
    return false;
  });

  await page.evaluate(
    () =>
      new Promise((resolve) => {
        requestAnimationFrame(() => requestAnimationFrame(resolve));
      }),
  );
}

function makeExpect(page) {
  return {
    async textContains(selectorOrLocator, substring, msg = "") {
      if (typeof selectorOrLocator === "string") {
        await page.waitForSelector(selectorOrLocator);
        const text = await page.locator(selectorOrLocator).innerText();
        if (!text.includes(substring)) {
          throw new Error(msg || `Expected ${selectorOrLocator} to contain '${substring}', got '${text}'`);
        }
        return;
      }
      const text = await selectorOrLocator.innerText();
      if (!text.includes(substring)) throw new Error(msg || `Expected locator to contain '${substring}', got '${text}'`);
    },
    async ok(cond, msg = "Expected condition to be true") {
      if (!cond) throw new Error(msg);
    },
    async bbox(locator, msg = "Expected locator to have a bounding box") {
      const b = await locator.boundingBox();
      if (!b) throw new Error(msg);
      return b;
    },
  };
}

async function runTestModule(testPath, ctx) {
  const url = pathToFileURL(path.resolve(testPath)).href;
  const mod = await import(url);
  const run = mod?.run || mod?.default;
  if (typeof run !== "function") throw new Error(`Expected ${testPath} to export 'run' (or default export)`);
  await run(ctx);
}

async function main() {
  const { url, test, size, timeoutMs, browserPath, headed, screenshot } = parseArgs(process.argv);
  const { width, height } = parseSize(size);

  const { chromium } = await import("playwright-core");
  const browser = await chromium.launch({
    headless: !headed,
    executablePath: browserPath || undefined,
    args: ["--no-sandbox", "--disable-dev-shm-usage", "--hide-scrollbars", `--window-size=${width},${height}`],
  });

  const ctx = await browser.newContext({ viewport: { width, height }, deviceScaleFactor: 1 });
  const page = await ctx.newPage();
  page.setDefaultTimeout(timeoutMs);

  const consoleErrors = [];
  page.on("pageerror", (err) => consoleErrors.push(String(err?.stack || err)));
  page.on("console", (msg) => {
    if (msg.type() === "error") consoleErrors.push(msg.text());
  });

  try {
    await page.goto(url, { waitUntil: "load" });
    await waitForCoiMount(page);

    const expect = makeExpect(page);
    if (test) {
      await runTestModule(test, { page, expect });
    }

    if (consoleErrors.length) {
      throw new Error(`Console errors:\n${consoleErrors.map((s) => `- ${s}`).join("\n")}`);
    }
  } catch (err) {
    if (screenshot) {
      try {
        await fs.mkdir(path.dirname(screenshot), { recursive: true });
        await page.screenshot({ path: screenshot });
      } catch {
        // ignore screenshot failures
      }
    }
    throw err;
  } finally {
    await page.close().catch(() => {});
    await ctx.close().catch(() => {});
    await browser.close().catch(() => {});
  }
}

await main().catch((err) => {
  console.error(err?.stack || String(err));
  process.exit(1);
});
