// a chain read inside a ternary, a prop and a select value updates on a change down the chain

export async function run({ page }) {
  const state = () => page.evaluate(() => [".plain", ".tern", ".shown"].map(s => document.querySelector(s).textContent).join(",") + "," + document.querySelector(".sel").value);
  if ((await state()) !== "a,z,z,z") throw new Error(`start: ${await state()}`);
  await page.locator(".edit").click();
  await page.waitForTimeout(50);
  if ((await state()) !== "a,a,a,a") throw new Error(`after edit: ${await state()}`);
  await page.locator(".change").click();
  await page.waitForTimeout(50);
  if ((await state()) !== "c,c,c,c") throw new Error(`after change: ${await state()}`);
}
