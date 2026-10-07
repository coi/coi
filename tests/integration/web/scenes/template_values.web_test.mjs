const text = (page, sel) => page.locator(sel).evaluate((el) => el.textContent.trim());

async function expectText(page, sel, want) {
  await page.waitForFunction(([s, w]) => document.querySelector(s)?.textContent.trim() === w, [sel, want])
    .catch(async () => { throw new Error(`${sel}: expected "${want}", got "${await text(page, sel)}"`); });
}

export async function run({ page }) {
  await expectText(page, ".flag", "on=true off=false");
  await page.locator(".flip").click();
  await expectText(page, ".flag", "on=false off=true");

  await expectText(page, ".count", "0");
  await page.locator(".add").click();
  await page.locator(".add").click();
  await expectText(page, ".count", "2");

  await expectText(page, ".size", "5x2");

  const title = await page.locator(".quoted").getAttribute("title");
  if (title !== 'say "hi"') throw new Error(`title: got ${JSON.stringify(title)}`);
}
