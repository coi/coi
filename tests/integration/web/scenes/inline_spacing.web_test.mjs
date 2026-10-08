export async function run({ page }) {
  const want = { a: "1 page", b: "1 page left", c: "1page", d: "1page", e: "x y" };
  for (const [cls, text] of Object.entries(want)) {
    const got = await page.locator(`.${cls}`).textContent();
    if (got !== text) throw new Error(`.${cls}: got "${got}", want "${text}"`);
  }
}
