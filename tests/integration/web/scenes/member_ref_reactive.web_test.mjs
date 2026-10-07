export async function run({ page }) {
  const expect = async (count, title, saves) => {
    await page.waitForFunction(([c, t, s]) => document.querySelector(".count").textContent === c && document.querySelector(".title").textContent === t && document.querySelector(".saves").textContent === s, [count, title, saves])
      .catch(async () => { throw new Error(`expected ${count}/${title}/${saves}, got ${await page.locator(".count").textContent()}/${await page.locator(".title").textContent()}/${await page.locator(".saves").textContent()}`); });
  };
  await expect("0", "a", "0");
  await page.locator(".add").click();
  await expect("1", "a", "1");
  await page.locator(".swap").click();
  await expect("2", "b", "1");
  await page.locator(".push").click();
  await expect("3", "b", "1");
  await page.locator(".add").click();
  await expect("4", "b", "2");
}
