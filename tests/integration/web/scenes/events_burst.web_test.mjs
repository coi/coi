export async function run({ page }) {
  await page.evaluate(() => {
    const t = document.querySelector(".pad");
    const ev = (name, x) => t.dispatchEvent(new PointerEvent(name, { bubbles: true, pointerId: 3, pointerType: "pen", clientX: x, clientY: 50, buttons: name === "pointerup" ? 0 : 1 }));
    ev("pointerdown", 10);
    for (let i = 0; i < 150; i++) ev("pointermove", 10 + i);
    ev("pointerup", 160);
  });
  await page.waitForFunction(() => document.querySelector(".out").textContent === "150 1", null, { timeout: 3000 })
    .catch(async () => { throw new Error(`got "${await page.locator(".out").textContent()}", want "150 1" (every move and the up, right away)`); });
}
