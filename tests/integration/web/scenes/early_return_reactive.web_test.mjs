export async function run({ page, expect }) {
  await page.locator(".bump").click();
  await expect.textContains(page.locator(".n"), "1");
  await page.locator(".bump").click();
  await expect.textContains(page.locator(".n"), "2");
  await page.locator(".toggle").click();
  await page.waitForFunction(() => document.querySelectorAll(".open").length === 1)
    .catch(() => { throw new Error("if region didn't open after an early return"); });
}
