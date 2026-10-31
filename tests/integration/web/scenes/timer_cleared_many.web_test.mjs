export async function run({ page }) {
  await page.waitForFunction(() => document.querySelector(".fired")?.textContent === "fired");
}
