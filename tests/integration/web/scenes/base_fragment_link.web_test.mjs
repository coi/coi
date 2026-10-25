// A fragment link stays on the page: path and query as they were, the hash set, the target
// scrolled to. With the base href alone the click would load the base's page instead.

export async function run({ page, expect }) {
  const at = new URL(page.url());
  await page.goto(at.origin + at.pathname + "?keep=1", { waitUntil: "load" });
  await page.waitForFunction(() => document.querySelector(".jump") !== null);
  await page.locator(".jump").click();
  await page.waitForFunction(() => location.hash === "#there");
  const where = await page.evaluate(() => [location.pathname, location.search, window.scrollY > 100]);
  await expect.ok(where[0] === at.pathname, `the path stays: ${where[0]}`);
  await expect.ok(where[1] === "?keep=1", `the query stays: ${where[1]}`);
  await expect.ok(where[2], "scrolled to the target");
}
