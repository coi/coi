export async function run({ page, expect }) {
  const rows = page.locator(".list .row");
  const ids = () => rows.evaluateAll((els) => els.map((e) => e.dataset.i).join(","));
  // every element added under the list from here on (setting a text puts in a new text node)
  await page.evaluate(() => {
    window.__added = 0;
    new MutationObserver((ms) => { for (const m of ms) for (const n of m.addedNodes) if (n.nodeType === 1) window.__added++; })
      .observe(document.querySelector(".list"), { childList: true, subtree: true });
    document.querySelectorAll(".list .row").forEach((e) => { e.__kept = true; });
    document.querySelectorAll(".list .tag").forEach((e) => { e.__kept = true; });
  });
  const kept = () => page.evaluate(() => [...document.querySelectorAll(".list .row")].every((e) => e.__kept));
  const added = () => page.evaluate(() => window.__added);

  await expect.ok((await ids()) === "0,1,2", `start: ${await ids()}`);

  // a change the rows read nothing of: nothing is made under the list
  await page.click(".bump");
  await expect.textContains(page.locator(".other"), "1");
  await expect.ok((await added()) === 0, `a change elsewhere added ${await added()} elements to the list`);

  // a value the rows show: set in place, rows and their components kept
  await page.click(".rename");
  await expect.ok((await rows.locator(".word").allTextContents()).join(",") === "b,b,b", "the word changed in every row");
  await expect.ok((await page.locator(".list .tag").allTextContents()).join(",") === "b0,b1,b2", "the components took the new label");
  await expect.ok(await kept(), "the rows were built again for a changed value");
  await expect.ok(await page.evaluate(() => [...document.querySelectorAll(".list .tag")].every((e) => e.__kept)), "a component was made again");
  await expect.ok((await added()) === 0, `a changed value added ${await added()} elements`);

  // an <if> in one row turns: that row only is built again, still the same element (morphed)
  await page.click(".toggle");
  await expect.ok((await page.locator(".list .extra").count()) === 1, "the open row shows its extra");
  await expect.textContains(page.locator(".list .row.open .extra"), "extra 1");
  await expect.ok(await kept(), "the row elements stay through a rebuild of one row");
  await page.click(".toggle");
  await expect.ok((await page.locator(".list .extra").count()) === 0, "closed again");

  // rows come and go at the end
  await page.click(".more");
  await expect.ok((await ids()) === "0,1,2,3", `more: ${await ids()}`);
  await page.click(".fewer");
  await page.click(".fewer");
  await expect.ok((await ids()) === "0,1", `fewer: ${await ids()}`);

  // the handlers have their row's index
  await rows.nth(1).click();
  await expect.textContains(page.locator(".clicked"), "1");

  // the window moves: the rows stand for new indexes, their handlers too
  await page.click(".shift");
  await expect.ok((await ids()) === "1,2", `shifted: ${await ids()}`);
  await rows.nth(0).click();
  await expect.textContains(page.locator(".clicked"), "1");
  await rows.nth(1).click();
  await expect.textContains(page.locator(".clicked"), "2");
  await expect.ok((await page.locator(".list .tag").allTextContents()).join(",") === "b1,b2", `labels after the shift: ${await page.locator(".list .tag").allTextContents()}`);
}
