export async function run({ page, expect }) {
  const rows = page.locator(".list .row");
  const ids = () => rows.evaluateAll((els) => els.map((e) => e.dataset.id).join(","));
  const titles = () => rows.locator(".title").allTextContents();
  // elements added under the list that aren't the first rows (a moved row counts as added)
  await page.evaluate(() => {
    window.__added = 0;
    new MutationObserver((ms) => { for (const m of ms) for (const n of m.addedNodes) if (n.nodeType === 1 && !n.__kept) window.__added++; })
      .observe(document.querySelector(".list"), { childList: true, subtree: true });
    document.querySelectorAll(".list .row").forEach((e) => { e.__kept = e.dataset.id; });
  });
  const kept = () => page.evaluate(() => [...document.querySelectorAll(".list .row")].every((e) => e.__kept === e.dataset.id));
  const added = () => page.evaluate(() => window.__added);

  await expect.ok((await ids()) === "1,2,3", `start: ${await ids()}`);

  await page.click(".bump");
  await expect.textContains(page.locator(".other"), "1");
  await expect.ok((await added()) === 0, `a change elsewhere added ${await added()} elements`);

  // a changed item: its row updates in place, and its handler has the new item
  await page.click(".rename");
  await expect.ok((await titles()).join(",") === "a,B,c", `titles: ${await titles()}`);
  await expect.ok(await kept(), "a row was made again for a changed title");
  await rows.nth(1).click();
  await expect.textContains(page.locator(".opened"), "B");

  // rows move with their keys, the same elements
  await page.click(".reverse");
  await expect.ok((await ids()) === "3,2,1", `reversed: ${await ids()}`);
  await expect.ok(await kept(), "a row was made again when moved");
  await expect.ok((await added()) === 0, `moving rows added ${await added()} new elements`);
  await rows.nth(0).click();
  await expect.textContains(page.locator(".opened"), "c");

  // an <if> in a row turns: that row is built again, still the same element
  await page.click(".pin");
  await expect.ok((await page.locator(".list .row[data-id='3'] .pinned").count()) === 1, "the first row shows pinned");
  await expect.ok(await kept(), "the pinned row's element stays");

  // push, pop, remove, clear
  await page.click(".add");
  await expect.ok((await ids()) === "3,2,1,4", `pushed: ${await ids()}`);
  await rows.nth(3).click();
  await expect.textContains(page.locator(".opened"), "n4");
  await page.click(".drop");
  await expect.ok((await ids()) === "3,2,1", `popped: ${await ids()}`);
  await page.click(".dropfirst");
  await expect.ok((await ids()) === "2,1", `removed first: ${await ids()}`);
  await rows.nth(0).click();
  await expect.textContains(page.locator(".opened"), "B");
  await page.click(".wipe");
  await expect.ok((await rows.count()) === 0, "cleared");
  await page.click(".add");
  await expect.ok((await ids()) === "5", `after clear: ${await ids()}`);
}
