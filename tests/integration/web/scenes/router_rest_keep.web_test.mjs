export async function run({ page }) {
  const text = (sel) => page.locator(sel).textContent();
  const path = () => page.evaluate(() => location.pathname);
  await page.evaluate(() => history.pushState({}, "", "/app"));
  await page.goBack(); await page.goForward();   // the router sees /app through popstate
  await page.waitForSelector(".app", { timeout: 3000 }).catch(() => { throw new Error("/app did not mount App (a \"/app/*\" route)"); });
  await page.locator(".bump").click(); await page.locator(".bump").click();
  await page.locator(".a").click();
  await page.waitForFunction(() => location.pathname === "/app/a");
  if ((await text(".clicks")) !== "2") throw new Error(`navigating to /app/a remounted App: clicks "${await text(".clicks")}", want "2"`);
  await page.locator(".b").click();
  await page.waitForFunction(() => location.pathname === "/app/b/deeper");
  if ((await text(".clicks")) !== "2") throw new Error(`navigating to /app/b/deeper remounted App`);
  await page.goBack();
  await page.waitForFunction(() => document.querySelector(".heard") && document.querySelector(".heard").textContent === "/app/a", null, { timeout: 3000 })
    .catch(async () => { throw new Error(`back: onPopstate heard "${await text(".heard")}", want "/app/a" (path ${await path()})`); });
  if ((await text(".clicks")) !== "2") throw new Error("back within /app/* remounted App");
  await page.locator(".home").click();
  await page.waitForSelector(".front", { timeout: 3000 }).catch(() => { throw new Error("navigating to / did not mount Front"); });
  if (await page.locator(".app").count()) throw new Error("App stayed mounted next to Front");
}
