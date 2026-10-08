// a ref written at the top refreshes bindings, loops and method calls two levels down

export async function run({ page, expect }) {
  await expect.textContains(page.locator(".leaf-n"), "1");
  await page.locator(".top-add").click();
  await page.waitForFunction(() => document.querySelector(".leaf-n")?.textContent === "2");
  await expect.textContains(page.locator(".mid-n"), "2");
  const items = await page.locator(".mid-item").count();
  if (items !== 2) throw new Error(`expected the mid loop to follow the ref, got ${items} items`);

  await page.keyboard.press("m");
  await page.waitForFunction(() => document.querySelector(".leaf-mode")?.textContent === "leaf on");

  await page.locator(".leaf-bump").click();
  await page.waitForFunction(() => document.querySelector(".top-n")?.textContent === "3");
  await expect.textContains(page.locator(".mid-n"), "3");
}
