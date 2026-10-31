// PointerFlags.NoMenu: the element's context menu is cancelled, a plain element's isn't,
// and the press itself still arrives with its buttons; text that can be edited inside the
// element keeps its own menu

export async function run({ page, expect }) {
  await page.evaluate(() => {
    window.__menus = [];
    document.addEventListener("contextmenu", (e) => { window.__menus.push(e.target.className + ":" + e.defaultPrevented); });
  });

  await page.locator(".pad").click({ button: "right", position: { x: 190, y: 90 } });
  await expect.textContains(page.locator(".pressed"), "2", "the right button is in buttons");
  await page.locator(".plain").click({ button: "right" });
  await page.locator(".field").click({ button: "right" });
  await page.locator(".note").click({ button: "right" });
  await page.waitForTimeout(100);
  const menus = await page.evaluate(() => window.__menus);
  if (JSON.stringify(menus) !== JSON.stringify(["pad:true", "plain:false", "field:false", "note:false"]))
    throw new Error(`the pad's menu is cancelled, the plain one's and its text's aren't: ${JSON.stringify(menus)}`);
}
