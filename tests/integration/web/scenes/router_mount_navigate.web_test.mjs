export async function run({ page }) {
  const where = () => page.evaluate(() => location.pathname);
  await page.locator(".enter").click();
  await page.waitForSelector(".inside", { timeout: 3000 }).catch(async () => { throw new Error(`navigate("/inside") from /gate's mount did not show Inside (at ${await where()})`); });
}
