// a socket closed by hand hears its close; a cleared timeout never fires
export const expectedConsoleErrors = ["ws://127.0.0.1:1/"];

export async function run({ page, expect }) {
  await page.click(".go");
  await page.waitForFunction(() => document.querySelector(".said")?.textContent.includes("closed yes"), null, { timeout: 5000 });
  await page.click(".go");
  await page.waitForFunction(() => (document.querySelector(".said")?.textContent.match(/closed/g) || []).length === 2, null, { timeout: 5000 });
  await page.waitForTimeout(200);
  await expect.ok((await page.locator(".fired").textContent()) === "0", "a cleared timeout fired");
  await expect.ok(!(await page.locator(".said").textContent()).includes("message"), "a message after close");
}
