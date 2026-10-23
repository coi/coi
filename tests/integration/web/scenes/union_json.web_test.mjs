export async function run({ page }) {
  const text = async (sel) => (await page.locator(sel).textContent()).trim();
  await page.locator(".round").waitFor();
  const want = {
    ".made": 'at 1,2 from me | {"type":"at","from":"me","x":1,"y":2}',
    ".parsed": "held 7 on (from present)",
    ".unknown": "error: No alternative of Message for this type",
    ".nested": "3 [left][at 3,4 from ][left] last=hold 1 off",
    ".round": "held 9 on from q",
  };
  for (const [sel, expected] of Object.entries(want)) {
    const got = await text(sel);
    if (got !== expected) throw new Error(`${sel}: got "${got}", want "${expected}"`);
  }
}
