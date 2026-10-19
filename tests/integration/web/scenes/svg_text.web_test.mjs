// svg <text> gets its static and its bound text, and the bound one follows changes

export async function run({ page }) {
  const text = (sel) => page.evaluate((s) => document.querySelector(s).textContent, sel);
  if ((await text(".fixed")) !== "label") throw new Error(`fixed: "${await text(".fixed")}"`);
  if ((await text(".bound")) !== "count 1") throw new Error(`bound: "${await text(".bound")}"`);
  await page.locator(".bump").click();
  await page.waitForFunction(() => document.querySelector(".bound").textContent === "count 2");
}
