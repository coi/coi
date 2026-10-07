export async function run({ page }) {
  const items = () => page.evaluate(() => Array.from(document.querySelectorAll(".n")).map((s) => s.textContent + ":" + s.title).join(","));
  await page.waitForFunction(() => document.querySelectorAll(".n").length === 2);
  if ((await items()) !== "alpha:alpha,beta:beta") throw new Error(`start: ${await items()}`);
  await page.locator(".add").click();
  await page.waitForFunction(() => document.querySelectorAll(".n").length === 3)
    .catch(async () => { throw new Error(`after add: ${await items()}`); });
  if ((await items()) !== "alpha:alpha,beta:beta,more0:more0") throw new Error(`after add: ${await items()}`);
}
