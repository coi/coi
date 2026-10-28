// PointerFlags.NoMenu: the element's context menu is cancelled, a plain element's isn't,
// and the press itself still arrives with its buttons

export async function run({ page, expect }) {
  await page.evaluate(() => {
    window.__menus = [];
    document.addEventListener("contextmenu", (e) => { window.__menus.push(e.target.className + ":" + e.defaultPrevented); });
  });

  await page.locator(".pad").click({ button: "right" });
  await expect.textContains(page.locator(".pressed"), "2", "the right button is in buttons");
  await page.locator(".plain").click({ button: "right" });
  await page.waitForTimeout(100);
  const menus = await page.evaluate(() => window.__menus);
  if (JSON.stringify(menus) !== JSON.stringify(["pad:true", "plain:false"]))
    throw new Error(`the pad's menu is cancelled, the plain one's isn't: ${JSON.stringify(menus)}`);
}
