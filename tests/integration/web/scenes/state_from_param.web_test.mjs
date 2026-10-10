export async function run({ page, expect }) {
  await expect.textContains(page.locator(".at"), "42");
  await expect.textContains(page.locator(".picked"), "first");
  await page.click(".next");
  await page.click(".bump");
  await expect.textContains(page.locator(".at"), "43");
}
