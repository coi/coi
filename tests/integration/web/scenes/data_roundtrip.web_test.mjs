export async function run({ page, expect }) {
  await page.waitForFunction(() => document.querySelector(".result")?.textContent !== "-", null, { timeout: 8000 });
  await expect.textContains(page.locator(".result"), "same");
  await expect.textContains(page.locator(".json"), '{"x":0.1,"y":0.3333333333333333,"pressure":0.5}');
  await expect.textContains(page.locator(".math"), "-544021 2356194 1024 -2 2 -3 3 5");
  await expect.textContains(page.locator(".numbers"), "0.30000000000000004 -0.5 3 1234567.89 e 5 1760000000123");
}
