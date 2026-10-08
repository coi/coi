export async function run({ page, expect }) {
  await expect.textContains(page.locator(".label"), "off");
  await page.locator(".q").fill("x");
  await page.waitForFunction(() => document.querySelector(".label")?.textContent === "on");
  await expect.textContains(page.locator(".hit"), "searching");
  const visible = await page.locator(".item:visible").count();
  if (visible !== 1) throw new Error(`expected one visible item while searching, got ${visible}`);
  await page.locator(".q").fill("");
  await page.waitForFunction(() => document.querySelector(".label")?.textContent === "off");
  const all = await page.locator(".item:visible").count();
  if (all !== 3) throw new Error(`expected all items back, got ${all}`);
}
