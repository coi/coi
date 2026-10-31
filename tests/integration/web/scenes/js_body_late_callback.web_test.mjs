// a JavaScript body's callback stops with its component: the child's late message is dropped,
// the app's arrives, the page keeps ticking, and nothing errors (the harness fails on page errors)

export async function run({ page, expect }) {
  await page.waitForFunction(() => document.querySelector(".kept")?.textContent === "late");
  await page.waitForFunction(() => parseInt(document.querySelector(".beats").textContent, 10) >= 5);
  await expect.ok((await page.locator(".child").count()) === 0, "the child is still on the page");
  await expect.ok(!(await page.evaluate(() => window.lateCalled === true)), "the gone child's callback was called");
}
