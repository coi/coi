export async function run({ page }) {
  const where = () => page.evaluate(() => location.pathname);
  const length = await page.evaluate(() => history.length);
  await page.locator(".enter").click();
  await page.waitForSelector(".inside", { timeout: 3000 }).catch(async () => { throw new Error(`redirect("/app") from /gate's mount did not mount Inside (at ${await where()})`); });
  if ((await where()) !== "/app") throw new Error(`the URL is "${await where()}", want "/app"`);
  const added = (await page.evaluate(() => history.length)) - length;
  if (added !== 1) throw new Error(`/gate and /app took ${added} history entries, want 1 (the redirect replaces /gate's)`);
  await page.goBack();
  await page.waitForSelector(".front", { timeout: 3000 }).catch(async () => { throw new Error(`back from /app did not reach Front (at ${await where()}): /gate is still in the history`); });
}
