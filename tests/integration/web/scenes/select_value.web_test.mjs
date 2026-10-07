export async function run({ page }) {
  await page.locator("select").waitFor();
  const v = () => page.locator("select").inputValue();
  if ((await v()) !== "16") throw new Error(`initial: ${await v()}`);
  await page.locator(".bump").click();
  await page.waitForFunction(() => document.querySelector("select").value === "24").catch(async () => { throw new Error(`after bump: ${await v()}`); });
  await page.locator("select").selectOption("12");
  await page.waitForFunction(() => document.querySelector(".size").textContent === "12");
}
