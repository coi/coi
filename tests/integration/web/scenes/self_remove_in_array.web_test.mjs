// a row removed by its own handler: the handler finishes on that row, the others stay as they were

const rows = (page) => page.evaluate(() => [...document.querySelectorAll(".row")].map(b => b.innerText));
const after = (page) => page.locator(".after").innerText();

export async function run({ page, expect }) {
  await page.waitForSelector(".row");
  let r = await rows(page);
  await expect.ok(r.join(" ") === "1:0 2:0 3:0 4:0", `at the start: ${r}`);

  // a middle row: the ones after it move up in the array while its handler is still running
  await page.locator(".row", { hasText: "2:0" }).click();
  r = await rows(page);
  await expect.ok(r.join(" ") === "1:0 3:0 4:0", `after removing 2: ${r}`);
  await expect.ok((await after(page)) === "2;", `the parent heard: ${await after(page)}`);

  // the last row: nothing moves into its slot
  await page.locator(".row", { hasText: "4:0" }).click();
  r = await rows(page);
  await expect.ok(r.join(" ") === "1:0 3:0", `after removing 4: ${r}`);
  await expect.ok((await after(page)) === "2;4;", `the parent heard: ${await after(page)}`);

  // the first row
  await page.locator(".row", { hasText: "1:0" }).click();
  r = await rows(page);
  await expect.ok(r.join(" ") === "3:0", `after removing 1: ${r}`);
  await expect.ok((await after(page)) === "2;4;1;", `the parent heard: ${await after(page)}`);
}
