async function until(page, fn, arg, what) {
  await page.waitForFunction(fn, arg, { timeout: 3000 }).catch(async () => { throw new Error(what()); });
}

export async function run({ page }) {
  await page.evaluate(() => {
    window.__changes = 0;
    new MutationObserver((ms) => { window.__changes += ms.length; })
      .observe(document.querySelector(".side"), { childList: true, subtree: true, attributes: true, characterData: true });
  });

  // the pod changes through the reference; the sibling's prop comes out the same
  for (let i = 0; i < 3; i++) await page.locator(".add").click();
  await until(page, () => document.querySelector(".n").textContent === "3", null, () => "the strokes didn't count up");
  await page.evaluate(() => new Promise((r) => setTimeout(r, 50)));
  const changes = await page.evaluate(() => window.__changes);
  if (changes !== 0) throw new Error(`the sibling re-rendered for a prop that didn't change: ${changes} DOM changes`);

  // a prop that does change still gets there
  await page.locator(".other").click();
  await until(page, () => document.querySelector('li[data-id="b"]').className === "on", null, () => "the new active id never reached the sibling");
}
