// Regression test: a logic-only state member component must be ticked every
// frame. Clicking samples the member's frame counter into the view; if tick
// was never forwarded to the member, the counter stays at 0.

export async function run({ page, expect }) {
  const sampled = page.locator(".sampled");

  // Nothing sampled yet.
  await expect.textContains(sampled, "-1");

  // Let a few frames run so the member's tick can accumulate.
  await page.waitForTimeout(300);

  const hit = page.locator(".hit");
  await hit.click();
  await page.waitForFunction(() => {
    const text = document.querySelector(".sampled")?.textContent ?? "";
    return parseInt(text, 10) > 0;
  });
}
