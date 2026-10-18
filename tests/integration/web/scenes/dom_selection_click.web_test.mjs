export async function run({ page }) {
  const log = () => page.locator(".log").textContent();
  const expect = async (want) => {
    await page.waitForFunction((w) => document.querySelector(".log").textContent === w, want)
      .catch(async () => { throw new Error(`log "${await log()}", want "${want}"`); });
  };
  await page.locator("#one").click({ position: { x: 5, y: 5 } });
  await expect("LI left plain");
  if (!(await page.locator("#one").evaluate((e) => e.classList.contains("done")))) throw new Error("li#one did not get class done");
  await page.locator("#b").click({ modifiers: ["Shift"] });
  await expect("B right shift");
  await page.locator("#one").click({ position: { x: 5, y: 5 } });
  if (await page.locator("#one").evaluate((e) => e.classList.contains("done"))) throw new Error("li#one kept class done after a second click");

  // select "bol" inside <b>, then ask from outside the editor (the button keeps the selection)
  await page.evaluate(() => { const t = document.getElementById("b").firstChild; const r = document.createRange(); r.setStart(t, 0); r.setEnd(t, 3); const s = getSelection(); s.removeAllRanges(); s.addRange(r); });
  await page.locator(".where").dispatchEvent("click");
  await expect("in bold [bol]");
  await page.evaluate(() => getSelection().removeAllRanges());
  await page.locator(".where").dispatchEvent("click");
  await expect("out plain []");
}
