export async function run({ page }) {
  const w = await page.evaluate(() => getComputedStyle(document.querySelector(".box")).width);
  if (w !== "50px") throw new Error(`.box width ${w}, want 50px (the @media rule after a comment)`);
  const css = await page.evaluate(async () => (await (await fetch("app.css")).text()));
  if (css.includes("note") || css.includes("phones")) throw new Error("comment text leaked into app.css: " + css.slice(0, 200));
}
