export async function run({ page, expect }) {
  const shown = (book) => page.locator(`.book[data-id='${book}'] .page:not(.hidden)`).evaluateAll((els) => els.map((e) => e.dataset.id).join(","));
  const all = (book) => page.locator(`.book[data-id='${book}'] .page`).evaluateAll((els) => els.map((e) => e.dataset.id).join(","));
  // elements added under the list that aren't the first rows (a moved row counts as added)
  await page.evaluate(() => {
    window.__added = 0;
    new MutationObserver((ms) => { for (const m of ms) for (const n of m.addedNodes) if (n.nodeType === 1 && !n.__kept) window.__added++; })
      .observe(document.querySelector(".list"), { childList: true, subtree: true });
    document.querySelectorAll(".list .book, .list .page, .list .mark").forEach((e) => { e.__kept = true; });
  });
  const added = () => page.evaluate(() => window.__added);
  const kept = () => page.evaluate(() => [...document.querySelectorAll(".list .book, .list .page, .list .mark")].every((e) => e.__kept));

  await expect.ok((await shown(1)) === "10,11" && (await shown(2)) === "12", `start: ${await shown(1)} | ${await shown(2)}`);
  await expect.ok((await all(1)) === "10,11,12", `every page in each book: ${await all(1)}`);

  await page.click(".bump");
  await expect.textContains(page.locator(".other"), "1");
  await expect.ok((await added()) === 0, `a change elsewhere added ${await added()} elements`);

  // an inner item changes: its rows (one per book) and their components update in place
  await page.click(".retitle");
  await expect.ok((await page.locator(".book[data-id='1'] .page[data-id='10'] .mark").textContent()) === "X", "the title in book 1");
  await expect.ok((await page.locator(".book[data-id='2'] .page[data-id='10'] .mark").textContent()) === "X", "the title in book 2");
  await expect.ok(await kept(), "an inner row or its component was made again");
  await expect.ok((await added()) === 0, `a changed title added ${await added()} elements`);

  // a page moves to the other book: classes change in place
  await page.click(".move");
  await expect.ok((await shown(1)) === "10" && (await shown(2)) === "11,12", `moved: ${await shown(1)} | ${await shown(2)}`);
  await expect.ok(await kept(), "moving a page made rows again");

  // the inner handler has both loop variables as they are now
  await page.locator(".book[data-id='2'] .page[data-id='11']").click();
  await expect.textContains(page.locator(".opened"), "B/y");
  await page.locator(".book[data-id='1'] .page[data-id='10']").click();
  await expect.textContains(page.locator(".opened"), "A/X");

  // inner rows move with their keys, the same elements
  await page.click(".flip");
  await expect.ok((await all(1)) === "12,11,10", `flipped: ${await all(1)}`);
  await expect.ok(await kept(), "flipping made rows again");

  // a range inside a row follows its count
  await page.click(".more");
  await expect.ok((await page.locator(".book[data-id='1'] .star").count()) === 2, "two stars");

  // a new inner item: a row in every book
  await page.click(".add");
  await expect.ok((await all(1)) === "12,11,10,13" && (await all(2)) === "12,11,10,13", `added: ${await all(1)} | ${await all(2)}`);

  // a book goes with its inner rows
  await page.click(".dropbook");
  await expect.ok((await page.locator(".book").count()) === 1 && (await page.locator(".page").count()) === 4, `after dropping a book: ${await page.locator(".book").count()} books, ${await page.locator(".page").count()} pages`);
  await page.locator(".book[data-id='2'] .page[data-id='13']").click();
  await expect.textContains(page.locator(".opened"), "B/p13");
}
