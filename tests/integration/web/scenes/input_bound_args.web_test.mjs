export async function run({ page }) {
  const out = () => page.locator(".out").textContent();
  await page.locator(".row1").fill("b");
  await page.locator(".row0").fill("a");
  await page.locator(".row1").press("Enter");
  await page.locator(".single").fill("x");
  await page.locator(".single").dispatchEvent("change");
  await page.waitForFunction(() => document.querySelector(".out").textContent === "a|b|solo:x|1013")
    .catch(async () => { throw new Error(`got: ${await out()}`); });
}
