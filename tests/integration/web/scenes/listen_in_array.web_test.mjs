// listeners of components in an array follow them when the array moves them, and go with them

const counts = (page) => page.evaluate(() => document.querySelector(".items").innerText.split(";").filter(Boolean));

export async function run({ page, expect }) {
  await page.locator(".add").click();
  await page.keyboard.press("k");
  await page.locator(".add").click();
  await page.keyboard.press("k");
  let c = await counts(page);
  await expect.ok(c.length === 80, `expected 80 items, got ${c.length}`);
  await expect.ok(c[0] === "1:2" && c[39] === "40:2" && c[40] === "41:1" && c[79] === "80:1", `after growing: ${c.slice(0, 2)} … ${c.slice(39, 42)} … ${c[79]}`);

  await page.locator(".drop").click();
  await page.locator(".drop").click();
  await page.keyboard.press("k");
  c = await counts(page);
  await expect.ok(c.length === 78 && c[0] === "3:3" && c[77] === "80:2", `after dropping: ${c.slice(0, 2)} … ${c[77]}`);

  await page.locator(".reset").click();
  await page.keyboard.press("k");
  await page.keyboard.press("k");
  c = await counts(page);
  await expect.ok(c.join(",") === "81:2,82:2,83:2", `after replacing: ${c}`);
}
