export async function run({ page }) {
  const cs = await page.evaluate(() => { const c = getComputedStyle(document.querySelector(".box")); return c.width + " " + c.height; });
  if (cs !== "50px 30px") throw new Error(`.box is ${cs}, want "50px 30px" (@container and @supports rules)`);
}
