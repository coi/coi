export async function run({ page }) {
  const text = async (sel) => (await page.locator(sel).textContent()).trim();
  const heard = [];
  page.on("console", (msg) => { if (msg.text().startsWith("keeper ")) heard.push(msg.text()); });
  // alive: both answer as usual
  await page.locator(".begin").click();
  await page.waitForFunction(() => document.querySelector(".opens")?.textContent === "1" && document.querySelector(".lates")?.textContent === "1", null, { timeout: 3000 })
    .catch(async () => { throw new Error(`a live hidden member didn't answer: opens ${await text(".opens")}, lates ${await text(".lates")}`); });
  // its owner gone before either answered: neither calls back
  const before = heard.length;
  await page.locator(".leave").click();
  await page.locator(".gone").waitFor();
  await page.waitForTimeout(500);
  const late = heard.slice(before);
  if (late.length) throw new Error(`a gone component was called back: ${late.join(", ")}`);
}
