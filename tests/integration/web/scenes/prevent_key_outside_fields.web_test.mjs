export async function run({ page, expect }) {
  // outside a field: Space on the scroller does nothing
  await page.click(".scroller", { position: { x: 300, y: 150 } });
  await page.keyboard.press("Space");
  await page.keyboard.press("Space");
  await page.waitForTimeout(200);
  const top = await page.evaluate(() => document.querySelector(".scroller").scrollTop);
  await expect.ok(top === 0, `Space scrolled the page outside a field (scrollTop ${top})`);
  // in a field it types
  await page.click(".field");
  await page.keyboard.type("a b");
  await expect.textContains(page.locator(".echo"), "a b");
}
