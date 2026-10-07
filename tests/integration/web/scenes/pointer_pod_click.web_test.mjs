// pod fields line up, x/y relative to the canvas

export async function run({ page, expect }) {
  await page.locator(".surface").click({ position: { x: 50, y: 40 } });
  await page.waitForFunction(() => document.querySelector(".up")?.textContent !== "-");
  await expect.textContains(page.locator(".down"), "50,40 mouse primary");
  await expect.textContains(page.locator(".up"), "50,40");
}
