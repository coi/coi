export async function run({ page }) {
  const text = (sel) => page.locator(sel).textContent();
  const where = () => page.evaluate(() => location.pathname + location.search + location.hash);
  await page.locator(".shop").click();
  await page.waitForSelector(".shop", { timeout: 3000 }).catch(async () => { throw new Error(`navigate("/shop?plan=year") did not mount Shop (at ${await where()})`); });
  if ((await where()) !== "/shop?plan=year") throw new Error(`the URL lost the query: "${await where()}", want "/shop?plan=year"`);
  if ((await text(".search")) !== "?plan=year") throw new Error(`the page reads "${await text(".search")}" as the search, want "?plan=year"`);
  await page.locator(".home").click();
  await page.waitForSelector(".front", { timeout: 3000 }).catch(() => { throw new Error("navigating to / did not mount Front"); });
  await page.locator(".top").click();
  await page.waitForSelector(".shop", { timeout: 3000 }).catch(async () => { throw new Error(`navigate("/shop#top") did not mount Shop (at ${await where()})`); });
  if ((await where()) !== "/shop#top") throw new Error(`the URL lost the hash: "${await where()}", want "/shop#top"`);
}
