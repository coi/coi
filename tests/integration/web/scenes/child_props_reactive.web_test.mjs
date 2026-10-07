export async function run({ page }) {
  const items = () => page.evaluate(() => Array.from(document.querySelectorAll(".item")).map((b) => b.textContent + (b.classList.contains("on") ? "*" : "")).join(","));
  const expect = async (want) => {
    await page.waitForFunction((w) => Array.from(document.querySelectorAll(".item")).map((b) => b.textContent + (b.classList.contains("on") ? "*" : "")).join(",") === w, want)
      .catch(async () => { throw new Error(`expected "${want}", got "${await items()}"`); });
  };
  await expect("A*");
  await page.locator(".add").click();
  await expect("A*,P1");
  await page.locator(".rename").click();
  await expect("Renamed*,P1");
  await page.locator(".item").nth(1).click();
  await expect("Renamed,P1*");
  await page.locator(".reset").click();
  await expect("Z");
}
