// Regression test: state assigned inside a match arm must update the DOM.

export async function run({ page, expect }) {
  const word = page.locator(".word");
  const parsed = page.locator(".parsed");

  // Initial state, before any match arm has run.
  await expect.textContains(word, "start");
  await expect.textContains(parsed, "none");

  const hit = page.locator(".hit");

  // First click: literal-match arm sets `word`, variant-match arm sets `parsed`.
  await hit.click();
  await page.waitForFunction(() => document.querySelector(".word")?.textContent === "one");
  await expect.textContains(word, "one");
  await expect.textContains(parsed, "parsed");

  // Second click: a different literal arm fires; the view must track it.
  await hit.click();
  await page.waitForFunction(() => document.querySelector(".word")?.textContent === "two");
  await expect.textContains(word, "two");

  // Third click: the `else` arm fires.
  await hit.click();
  await page.waitForFunction(() => document.querySelector(".word")?.textContent === "many");
  await expect.textContains(word, "many");
}
