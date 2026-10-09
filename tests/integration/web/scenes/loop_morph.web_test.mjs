const ids = (page) => page.$$eval(".list > li", (els) => els.map((e) => e.dataset.id).join(""));

async function until(page, fn, arg, what) {
  await page.waitForFunction(fn, arg, { timeout: 3000 }).catch(async () => { throw new Error(what()); });
}

export async function run({ page }) {
  // a class change keeps the row's node
  await page.$$eval(".list > li", (els) => els.forEach((e) => { e.__mark = e.dataset.id; }));
  await page.locator('li[data-id="b"] .sel').click();
  await until(page, () => document.querySelector('li[data-id="b"]').className.includes("on"), null, () => "row b not selected");
  const kept = await page.$$eval(".list > li", (els) => els.map((e) => e.__mark || "-").join(""));
  if (kept !== "abc") throw new Error(`rows were rebuilt on a class change: ${kept}`);

  // pointer capture outlives the re-render that grabbing causes
  const grip = await page.locator('li[data-id="a"] .grip').boundingBox();
  await page.mouse.move(grip.x + 5, grip.y + 5);
  await page.mouse.down();
  for (let i = 1; i <= 6; i++) await page.mouse.move(grip.x + 5, grip.y + 5 + i * 40);
  await page.mouse.up();
  await until(page, () => document.querySelector(".ups").textContent === "1", null, () => "pointerup lost after the row re-rendered");
  const moves = Number(await page.locator(".moves").textContent());
  if (moves < 5) throw new Error(`pointer moves lost after the row re-rendered: ${moves}`);

  // handlers of nested rows don't pile up: hundreds of re-renders, nested buttons still answer
  for (let i = 0; i < 120; i++) await page.locator(".rerender").click();
  await page.locator('li[data-id="a"] .tag', { hasText: "a2" }).click();
  await until(page, () => document.querySelector(".picked").textContent === "a2", null, () => "nested handler dead after many re-renders");

  // reorder: same nodes, new order
  await page.locator(".reverse").click();
  await until(page, () => Array.from(document.querySelectorAll(".list > li")).map((e) => e.dataset.id).join("") === "cba", null, () => "not reversed");
  const moved = await page.$$eval(".list > li", (els) => els.map((e) => e.__mark || "-").join(""));
  if (moved !== "cba") throw new Error(`rows were rebuilt on a reorder: ${moved}`);

  // a removed row goes; the nested handlers left call back with their own item
  await page.locator(".dropfirst").click();
  await until(page, () => Array.from(document.querySelectorAll(".list > li")).map((e) => e.dataset.id).join("") === "ba", null, () => "first row not removed");
  await page.locator('li[data-id="b"] .tag').click();
  await until(page, () => document.querySelector(".picked").textContent === "b1", null, () => "nested handler of a kept row calls the wrong item");
  if ((await ids(page)) !== "ba") throw new Error("order changed");
}
