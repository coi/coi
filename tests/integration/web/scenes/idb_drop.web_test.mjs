// drop waits for every connection to close, then deletes

export async function run({ page, expect }) {
  await page.waitForFunction(() => document.querySelector(".left")?.textContent !== "-", null, { timeout: 10000 });
  await expect.textContains(page.locator(".first"), "in use");
  await expect.textContains(page.locator(".second"), "dropped");
  await expect.textContains(page.locator(".left"), "empty");
}
