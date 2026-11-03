export async function run({ page }) {
  const count = () => page.locator(".holder canvas").count();
  await page.locator(".add").click();
  await page.locator(".add").click();
  if ((await count()) !== 2) throw new Error(`appended ${await count()}, want 2`);
  await page.locator(".toggle").click();
  await page.locator(".holder.wide").waitFor();
  await page.locator(".toggle").click();
  await page.locator(".renders", { hasText: "2" }).waitFor();
  if ((await count()) !== 2) throw new Error(`after re-renders ${await count()}, want 2`);
  await page.locator(".take").click();
  await page.waitForFunction(() => document.querySelectorAll(".holder canvas").length === 1);
  const id = await page.evaluate(() => document.querySelector(".holder canvas").id);
  if (id !== "made-1") throw new Error(`left ${id}, want made-1`);
}
