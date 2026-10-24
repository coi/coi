// the same as the browser's encodeURIComponent, and back

export async function run({ page, expect }) {
  const raw = "a+b@x.com & 50% ü/?#";
  const enc = await page.locator(".enc").innerText();
  const want = await page.evaluate((s) => encodeURIComponent(s), raw);
  await expect.ok(enc === want, `encode: ${enc} vs ${want}`);
  await expect.ok((await page.locator(".dec").innerText()) === raw, "decode gives the text back");
  await expect.ok((await page.locator(".keep").innerText()) === "A-z_0.9!~*'()", "unreserved characters stay");
}
