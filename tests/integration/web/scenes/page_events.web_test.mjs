// page-wide handlers, destroyed component stops getting events

export async function run({ page, expect }) {
  await page.context().grantPermissions(["clipboard-read", "clipboard-write"]);

  await page.keyboard.press("a");
  await expect.textContains(page.locator(".key"), "a");
  await expect.textContains(page.locator(".counter"), "1");

  // Ctrl+S is prevented (no save dialog) and still reported
  await page.keyboard.press("Control+s");
  await expect.textContains(page.locator(".key"), "ctrl+s");
  // Control and S are two key-downs
  await expect.textContains(page.locator(".counter"), "3");

  // counter is gone, its handler must not run
  await page.locator(".hide").click();
  await page.keyboard.press("b");
  await expect.textContains(page.locator(".key"), "b");
  const counters = await page.locator(".counter").count();
  if (counters !== 0) throw new Error("counter still shown");

  await page.evaluate(() => {
    const dt = new DataTransfer();
    dt.setData("text/plain", "hello paste");
    document.body.dispatchEvent(new ClipboardEvent("paste", { clipboardData: dt, bubbles: true, cancelable: true }));
  });
  await expect.textContains(page.locator(".pasted"), "hello paste");

  await page.evaluate(() => {
    Object.defineProperty(document, "visibilityState", { value: "hidden", configurable: true });
    Object.defineProperty(document, "hidden", { value: true, configurable: true });
    document.dispatchEvent(new Event("visibilitychange"));
  });
  await expect.textContains(page.locator(".vis"), "hidden");

  await page.locator(".copy").click();
  await page.waitForFunction(async () => (await navigator.clipboard.readText()) === "copied from coi");
}
