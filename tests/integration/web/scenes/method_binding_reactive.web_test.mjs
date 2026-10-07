export async function run({ page, expect }) {
  const on = () => page.evaluate(() => Array.from(document.querySelectorAll(".opt")).map((b) => b.classList.contains("on") ? "1" : "0").join(""));
  await page.waitForFunction(() => document.querySelectorAll(".opt").length === 2);
  if ((await on()) !== "10") throw new Error(`start: ${await on()}`);
  await page.locator(".opt").nth(1).click();
  await page.waitForFunction(() => document.querySelectorAll(".opt")[1].classList.contains("on") && !document.querySelectorAll(".opt")[0].classList.contains("on"))
    .catch(async () => { throw new Error(`after choose: ${await on()}`); });
  await page.locator(".rename").click();
  await expect.textContains(page.locator(".label"), "name:b");
}
