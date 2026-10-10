export async function run({ page, expect }) {
  const root = new URL("/", page.url()).href;

  // the served HTML already has the page, computed parts included
  const html = await (await page.request.get(root)).text();
  await expect.ok(html.includes('<h1 class="hi">Hello</h1>'), "first render not in the served HTML");
  await expect.ok(html.includes(">offline</li>") && html.includes(">2026</p>"), "loop or method output not prerendered");
  const tool = await (await page.request.get(root + "tool/")).text();
  await expect.ok(tool.includes('<button class="count">0</button>'), "second route not prerendered");

  // after the takeover: one copy, styled, and alive
  await page.waitForFunction(() => !document.getElementById("coi-pre"));
  await expect.ok(await page.locator(".site").count() === 1, "prerendered copy left next to the app");
  await expect.ok(await page.locator(".tile").first().evaluate((e) => getComputedStyle(e).color) === "rgb(1, 2, 3)", "CSS missing");
  await page.click(".like");
  await expect.textContains(page.locator(".like"), "1");

  // a folder route, with its trailing slash
  await page.goto(root + "tool/");
  await page.waitForFunction(() => !document.getElementById("coi-pre"));
  await expect.ok(await page.locator(".count").count() === 1, "tool page doubled or missing");
  await page.click(".count");
  await expect.textContains(page.locator(".count"), "1");
}
