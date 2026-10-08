export async function run({ page }) {
  const text = async (sel) => (await page.locator(sel).textContent()).trim();
  const open = async () => {
    await page.locator(".show").click();
    await page.locator(".draft").waitFor();
    if (!(await page.evaluate(() => document.activeElement?.tagName === "INPUT"))) throw new Error("input not focused on mount");
  };
  await open();
  await page.keyboard.type("first");
  if ((await text(".mounts")) !== "1") throw new Error(`mounts ${await text(".mounts")}, want 1`);
  await page.locator(".done").click();
  await page.locator(".closed").waitFor();
  if ((await text(".last")) !== "first") throw new Error(`last ${await text(".last")}`);
  await open();
  const value = await page.locator(".draft input").inputValue();
  if (value !== "") throw new Error(`reopened draft kept "${value}"`);
  if ((await text(".mounts")) !== "1") throw new Error(`fresh instance has mounts ${await text(".mounts")}, want 1`);
  await page.keyboard.type("second");
  await page.locator(".done").click();
  await page.locator(".closed").waitFor();
  if ((await text(".last")) !== "second") throw new Error(`last ${await text(".last")}`);
}
