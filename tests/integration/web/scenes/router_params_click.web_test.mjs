// Regression test for dynamic route params: an int param is parsed and passed to
// the routed component, and a value that fails to parse falls through to `else`.
//
// The nav buttons live outside <route/>, so they are always present regardless of
// which route is active. We drive navigation explicitly rather than relying on the
// initial served path.

export async function run({ page, expect }) {
  // Navigate to a dynamic route: /users/42 -> User with id = 42.
  await page.locator(".to-user").click();
  await page.waitForFunction(() => document.querySelector(".usr") !== null);
  await expect.textContains(page.locator(".uid"), "42");

  // Navigate to "/" -> Home.
  await page.locator(".to-home").click();
  await page.waitForFunction(() => document.querySelector(".home") !== null);
  await expect.textContains(page.locator(".page"), "Home");

  // "abc" cannot parse as int, so /users/abc does not match; else -> NotFound.
  await page.locator(".to-bad").click();
  await page.waitForFunction(() => document.querySelector(".nf") !== null);
  await expect.textContains(page.locator(".page"), "Not Found");
}
