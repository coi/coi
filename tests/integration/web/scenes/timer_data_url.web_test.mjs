export async function run({ page, expect }) {
  await expect.textContains(page.locator(".url"), "data:image/png");
  await page.waitForFunction(() => document.querySelector(".fired")?.textContent === "fired");
  await page.waitForTimeout(150);
  await expect.textContains(page.locator(".wrong"), "-");
}
