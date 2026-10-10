export async function run({ page, expect }) {
  const on = () => page.evaluate(() => Array.from(document.querySelectorAll(".opt.on")).map((b) => b.textContent).join(","));
  await expect.ok((await on()) === "Dots", `expected Dots on at start, got "${await on()}"`);
  await page.click(".lines");
  await expect.ok((await on()) === "Lines", `expected Lines on after the click, got "${await on()}"`);
  await page.click(".dots");
  await expect.ok((await on()) === "Dots", `expected Dots on again, got "${await on()}"`);
}
