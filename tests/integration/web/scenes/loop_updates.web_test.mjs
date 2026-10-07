const text = (page, sel) => page.locator(sel).evaluate((el) => el.textContent.replace(/\s+/g, ""));

async function expectBoth(page, want) {
  await page.waitForFunction((w) => {
    const t = (s) => document.querySelector(s).textContent.replace(/\s+/g, "");
    return t(".first") === w && t(".second") === w;
  }, want).catch(async () => {
    throw new Error(`expected both loops "${want}", got "${await text(page, ".first")}" / "${await text(page, ".second")}"`);
  });
}

export async function run({ page }) {
  await expectBoth(page, "abc");
  await page.locator(".pop").click();
  await expectBoth(page, "ab");
  await page.locator(".push").click();
  await expectBoth(page, "abn1");
  await page.locator(".replace").click();
  await expectBoth(page, "xy");

  await page.locator(".chips").click();
  await page.waitForFunction(() => document.querySelectorAll(".chipbox .chip").length === 3)
    .catch(async () => { throw new Error(`chips after assign: ${await page.locator(".chipbox .chip").count()}`); });
  await page.locator(".toggle").click();
  await page.locator(".toggle").click();
  await page.waitForFunction(() => document.querySelectorAll(".chipbox .chip").length === 3)
    .catch(async () => { throw new Error(`chips after reopening: ${await page.locator(".chipbox .chip").count()}`); });
  await page.locator(".chips").click();
  await page.waitForFunction(() => document.querySelectorAll(".chipbox .chip").length === 3)
    .catch(async () => { throw new Error(`chips after second assign: ${await page.locator(".chipbox .chip").count()}`); });
}
