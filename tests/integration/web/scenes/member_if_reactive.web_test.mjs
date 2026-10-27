// an <if> on the owner's method over a member's pod shows once the member fills the pod

export async function run({ page }) {
  const state = () => page.evaluate(() => [
    document.querySelector(".text").textContent,
    document.querySelector(".banner") ? "banner" : "none",
  ].join(","));
  if ((await state()) !== ",none") throw new Error(`start: ${await state()}`);
  await page.locator(".fill").click();
  await page.waitForTimeout(50);
  if ((await state()) !== "over,banner") throw new Error(`after fill: ${await state()}`);
}
