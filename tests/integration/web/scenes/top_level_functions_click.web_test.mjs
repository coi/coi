// Regression test: top-level functions compute the initial values, a method
// shadows the free function of the same name, and mutating a pod through a
// `mut Vec2&` param updates the view.

export async function run({ page, expect }) {
  await expect.textContains(page.locator(".size"), "25");
  await expect.textContains(page.locator(".pos"), "3,4");
  await expect.textContains(page.locator(".who"), "method");

  await page.locator(".hit").click();
  await expect.textContains(page.locator(".pos"), "6,8");
  await expect.textContains(page.locator(".size"), "100");
}
