// bytes survive idb and a Blob

export async function run({ page, expect }) {
  await expect.textContains(page.locator(".blob"), "3");
  await page.waitForFunction(() => document.querySelector(".keys")?.textContent !== "-");
  await expect.textContains(page.locator(".opened"), "yes");
  await expect.textContains(page.locator(".stored"), "done");
  await expect.textContains(page.locator(".sum"), "4 bytes, sum 256");
  await expect.textContains(page.locator(".missing"), "not found");
  // keys arrive sorted, one per line
  const keys = await page.locator(".keys").evaluate((el) => el.innerText.split(/\s+/).filter(Boolean));
  if (keys.join(",") !== "note/a,note/b") throw new Error(`keys: ${keys.join(",")}`);
}
