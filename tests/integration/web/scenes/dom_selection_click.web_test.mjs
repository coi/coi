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
  if (await page.locator(".press").textContent() !== "LI plain") throw new Error(`press "${await page.locator(".press").textContent()}", want "LI plain"`);
  if (!(await page.evaluate(() => document.activeElement === document.querySelector(".ed")))) throw new Error("a click in the text did not focus the editor");

  // a link: the press makes the editor read-only, so it loses focus and gets no caret, and the click isn't followed
  await page.locator("#ln").click({ position: { x: 5, y: 5 }, modifiers: ["Control"] });
  await expect("A left plain");
  if (await page.locator(".press").textContent() !== "A ctrl") throw new Error(`press "${await page.locator(".press").textContent()}", want "A ctrl"`);
  if (await page.evaluate(() => document.activeElement === document.querySelector(".ed"))) throw new Error("a press on the link focused the editor");
  await page.locator("#ln").click({ position: { x: 5, y: 5 } });
  await expect("A left plain");
  if (await page.evaluate(() => location.hash) !== "") throw new Error("the link was followed");
  if (await page.locator(".ed").getAttribute("contenteditable") !== "true") throw new Error("the editor stayed read-only after the click");

  // select "bol" inside <b>, then ask from outside the editor (the button keeps the selection)
  await page.evaluate(() => { const t = document.getElementById("b").firstChild; const r = document.createRange(); r.setStart(t, 0); r.setEnd(t, 3); const s = getSelection(); s.removeAllRanges(); s.addRange(r); });
  await page.locator(".where").dispatchEvent("click");
  await expect("in bold [bol]");
  await page.evaluate(() => getSelection().removeAllRanges());
  await page.locator(".where").dispatchEvent("click");
  await expect("out plain []");

  // the span under the cover, and nothing outside the element
  await page.locator(".probe").dispatchEvent("click");
  await expect("deep none");
}
