export async function run({ page, expect }) {
  const covers = page.locator(".grid .cover");
  // a prop change keeps the row's element
  await page.evaluate(() => { document.querySelector(".grid .cover").dataset.mark = "kept"; });
  await page.click(".bump");
  await expect.ok(await page.evaluate(() => document.querySelector(".grid .cover").dataset.mark === "kept"), "row element was rebuilt on a prop change");
  await expect.textContains(covers.nth(0), "A 1");
  // pressing changes state the rows read (held); the click still lands on the same row
  await covers.nth(1).click();
  await expect.textContains(page.locator(".opened"), "b");
  await expect.ok(await covers.nth(1).evaluate((e) => e.classList.contains("held")), "held prop not applied");
  await page.click(".bump");
  await page.click(".add");
  await page.click(".bump");
  await page.click(".add");
  await page.click(".bump");
  await expect.ok((await covers.count()) === 4, `expected 4 covers after adds, got ${await covers.count()}`);
  await expect.textContains(covers.nth(0), "A 4");

  await page.click(".drop");
  await page.click(".bump");
  await expect.ok((await covers.count()) === 3, `expected 3 covers after drop, got ${await covers.count()}`);
  await expect.textContains(covers.nth(0), "B 5");

  // the rebuilt rows still take clicks
  await covers.nth(0).click();
  await expect.textContains(page.locator(".opened"), "b");

  // a bigger rebuild: every row still answers for itself
  for (let i = 0; i < 6; i++) await page.click(".add");
  await page.click(".bump");
  const ids = await covers.evaluateAll((els) => els.map((e) => e.dataset.id));
  for (let i = 0; i < ids.length; i++) {
    await covers.nth(i).click();
    await expect.textContains(page.locator(".opened"), ids[i]);
  }
}
