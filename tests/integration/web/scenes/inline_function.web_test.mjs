export async function run({ page }) {
  const text = async (sel) => (await page.locator(sel).textContent()).trim();
  await page.locator(".removed").waitFor();
  const want = {
    ".found": "1 -1 true 2 false",
    ".counted": "2 2 true false",
    ".kept": "2 1,9,",
    ".removed": "1 1,",
  };
  for (const [sel, expected] of Object.entries(want)) {
    const got = await text(sel);
    if (got !== expected) throw new Error(`${sel}: got "${got}", want "${expected}"`);
  }
}
