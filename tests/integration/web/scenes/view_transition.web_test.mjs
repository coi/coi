export async function run({ page, expect }) {
  // the change comes a frame later, once the browser has kept the old page as a picture
  const state = (text) => page.waitForFunction((t) => document.querySelector(".state").textContent.includes(t), text, { timeout: 3000 });
  await page.click(".toggle");
  await state("dark 1");
  await expect.ok(await page.locator(".page.dark").count() === 1, "class not switched to dark");
  // the transition ended (done was called), so the page takes input and a second one works too
  await page.waitForFunction(() => !document.getAnimations().some((a) => String(a.effect?.pseudoElement || "").startsWith("::view-transition")), null, { timeout: 3000 });
  await page.click(".toggle");
  await state("light 2");
  await expect.ok(await page.locator(".page.dark").count() === 0, "class not back to light");
}
