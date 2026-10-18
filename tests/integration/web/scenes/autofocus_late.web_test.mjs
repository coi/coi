export async function run({ page }) {
  const focused = () => page.evaluate(() => document.activeElement.className);
  await page.locator(".edit").click();
  await page.waitForFunction(() => document.activeElement.className === "field", null, { timeout: 2000 })
    .catch(async () => { throw new Error(`after the <if>: focus on "${await focused()}", want the input`); });
  await page.locator(".go-b").click();
  await page.waitForFunction(() => document.activeElement.className === "in-b", null, { timeout: 2000 })
    .catch(async () => { throw new Error(`in the loop row: focus on "${await focused()}", want in-b`); });
}
