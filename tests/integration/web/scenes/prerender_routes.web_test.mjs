export async function run({ page, expect }) {
  const root = new URL("/", page.url()).href;

  // the served HTML already has the page, computed parts included
  const html = await (await page.request.get(root)).text();
  await expect.ok(html.includes('<h1 class="hi">Hello</h1>'), "first render not in the served HTML");
  await expect.ok(html.includes(">offline</li>") && html.includes(">2026</p>"), "loop or method output not prerendered");
  const tool = await (await page.request.get(root + "tool/")).text();
  await expect.ok(tool.includes('<button class="count">0</button>'), "second route not prerendered");
  // a "/docs/*" route: its prefix page, and the listed /docs/guide rendered with that path
  const docs = await (await page.request.get(root + "docs/")).text();
  await expect.ok(docs.includes('<p class="at">/docs</p>'), "the /docs/* route's prefix page not prerendered");
  const guide = await (await page.request.get(root + "docs/guide/")).text();
  await expect.ok(guide.includes('<p class="at">/docs/guide</p>'), "a listed prerender path not rendered with its own pathname");
  // served for another path, a page keeps its markup hidden until the app takes over
  await expect.ok(guide.includes('var r="/docs/guide"') && guide.includes("data-coi-other-route"), "no route guard in a prerendered page");

  // hydration: the elements that arrived as HTML are the ones the app runs on. The wasm is
  // held back until the page has been marked, then let through
  let release;
  const held = new Promise((r) => (release = r));
  await page.route("**/app.wasm", async (route) => { await held; await route.continue(); });
  await page.goto(root);
  await page.waitForSelector("[data-hydrate] .hi");
  await page.evaluate(() => { document.querySelector(".hi").__mark = "kept"; });
  release();
  await page.waitForFunction(() => !document.querySelector("[data-hydrate]"));
  await expect.ok(await page.evaluate(() => document.querySelector(".hi").__mark === "kept"), "prerendered elements were replaced, not taken over");
  await expect.ok(await page.locator(".site").count() === 1, "a second copy of the page");
  await expect.ok(await page.locator(".tile").first().evaluate((e) => getComputedStyle(e).color) === "rgb(1, 2, 3)", "CSS missing");
  await page.click(".like");
  await expect.textContains(page.locator(".like"), "1");
  await page.unroute("**/app.wasm");

  // a route that is one child component: rendered in its place, before the route's anchor
  const wrapped = await (await page.request.get(root + "wrapped/")).text();
  await expect.ok(/<section class="panel"><p class="shown">on<\/p><!--coi-⚓--><\/section><!--coi-route-->/.test(wrapped), "a child-rooted route not prerendered in place");
  await page.goto(root + "wrapped/");
  await page.waitForFunction(() => !document.querySelector("[data-hydrate]"));
  await expect.ok(await page.locator(".panel").count() === 1 && await page.locator(".shown").count() === 1, "child-rooted route doubled or missing");

  // a mount that navigates away: the page rendered at build is its own, the browser redirects
  const gate = await (await page.request.get(root + "gate/")).text();
  await expect.ok(gate.includes('<p class="gate">build</p>'), "a page whose mount redirects was rendered as the redirect's target");
  await page.goto(root + "gate/");
  await page.waitForFunction(() => location.pathname === "/tool" || location.pathname === "/tool/");

  // a folder route, with its trailing slash
  await page.goto(root + "tool/");
  await page.waitForFunction(() => !document.querySelector("[data-hydrate]"));
  await expect.ok(await page.locator(".count").count() === 1, "tool page doubled or missing");
  await page.click(".count");
  await expect.textContains(page.locator(".count"), "1");

  // a canvas drawn on before hydration: the prerendered canvas gives way to the one with the drawing
  await page.route("**/app.wasm", async (route) => { await held2; await route.continue(); });
  let release2;
  const held2 = new Promise((r) => (release2 = r));
  await page.goto(root + "sketch/");
  await page.waitForSelector("[data-hydrate] canvas.sketch");
  release2();
  await page.waitForFunction(() => !document.querySelector("[data-hydrate]"));
  await expect.ok(await page.locator("canvas.sketch").count() === 1, "canvas doubled or missing");
  const red = await page.evaluate(() => { const c = document.querySelector("canvas.sketch"); return c.getContext("2d").getImageData(5, 5, 1, 1).data[0]; });
  await expect.ok(red === 255, "the canvas on the page is not the one drawn on");
  await page.unroute("**/app.wasm");
}
