export async function run({ page }) {
  const s = await page.evaluate(() => { const c = getComputedStyle(document.querySelector(".dot")); return { w: c.width, h: c.height }; });
  if (s.w !== "50px") throw new Error(`.dot width ${s.w}, want 50px (".outer:not(.off) .dot" scoped on .dot)`);
  if (s.h !== "30px") throw new Error(`.dot height ${s.h}, want 30px (the colon inside [data-k="a:b"] is not a pseudo)`);
}
