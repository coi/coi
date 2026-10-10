export async function run({ page, expect }) {
  const covers = page.locator(".grid .cover");
  await page.click(".add");
  await page.click(".bump");
  await page.click(".add");
  await page.click(".bump");
  await expect.ok((await covers.count()) === 4, `expected 4 covers after adds, got ${await covers.count()}`);
  await expect.textContains(covers.nth(0), "A 2");

  await page.click(".drop");
  await page.click(".bump");
  await expect.ok((await covers.count()) === 3, `expected 3 covers after drop, got ${await covers.count()}`);
  await expect.textContains(covers.nth(0), "B 3");

  // the rebuilt rows still take clicks
  await covers.nth(0).click();
  await expect.textContains(page.locator(".opened"), "b");
}
