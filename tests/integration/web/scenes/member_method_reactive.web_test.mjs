// a member's method in text, a prop, an own method and a method chain all update with the member

export async function run({ page }) {
  const state = () => page.evaluate(() => [
    document.querySelector(".text").textContent,
    document.querySelector(".shown").textContent,
    document.querySelector(".own").classList.contains("out") ? "out" : "in",
    document.querySelector(".busy").textContent,
  ].join(","));
  if ((await state()) !== "in,in,in,in") throw new Error(`start: ${await state()}`);
  await page.locator(".close").click();
  await page.waitForTimeout(50);
  if ((await state()) !== "out,out,out,out") throw new Error(`after close: ${await state()}`);
}
