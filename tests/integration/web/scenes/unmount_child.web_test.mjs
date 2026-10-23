export async function run({ page }) {
  const text = async (sel) => (await page.locator(sel).textContent()).trim();
  // shown and hidden before its 400 ms timer fires: unmount ran, the timer never did
  await page.locator(".show").click();
  await page.locator(".ticker").waitFor();
  await page.locator(".hide").click();
  await page.locator(".ticker").waitFor({ state: "detached" });
  if ((await text(".gone")) !== "1") throw new Error(`gone ${await text(".gone")}, want 1`);
  await page.waitForTimeout(700);
  if ((await text(".ticks")) !== "0") throw new Error(`a dead child's timer fired: ticks ${await text(".ticks")}`);
  // shown and left alone: the timer fires as usual
  await page.locator(".show").click();
  await page.locator(".ticker").waitFor();
  await page.waitForTimeout(700);
  if ((await text(".ticks")) !== "1") throw new Error(`live child's timer didn't fire: ticks ${await text(".ticks")}`);
  await page.locator(".hide").click();
  await page.locator(".ticker").waitFor({ state: "detached" });
  if ((await text(".gone")) !== "2") throw new Error(`gone ${await text(".gone")}, want 2`);
}
