// A click on the canvas reaches onPointer with phase Down then Up, type Mouse and
// the primary button set: the enum and flags values survive the trip through webcc.

export async function run({ page, expect }) {
  await page.locator(".surface").click({ position: { x: 50, y: 40 } });
  await page.waitForFunction(() => document.querySelector(".log")?.textContent?.trim() === "down up");
  await expect.textContains(page.locator(".kind"), "mouse");
  await expect.textContains(page.locator(".tip"), "primary");
}
