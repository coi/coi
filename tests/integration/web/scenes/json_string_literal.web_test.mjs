// the same as JSON.stringify in the browser

export async function run({ page, expect }) {
  const lit = await page.locator(".lit").innerText();
  const v = await page.locator(".var").innerText();
  await expect.ok(lit === JSON.stringify('a "b" \\ c'), `literal: ${lit}`);
  await expect.ok(v === JSON.stringify('say "hi"'), `variable: ${v}`);
}
