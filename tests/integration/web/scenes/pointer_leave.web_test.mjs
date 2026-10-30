// onpointerleave: fires when the pointer goes off the element, not when it moves onto a child

export async function run({ page, expect }) {
  const box = await page.locator(".pad").boundingBox();
  await page.mouse.move(box.x + 150, box.y + 80);
  await page.waitForFunction(() => document.querySelector(".at")?.textContent === "150 80");
  await page.mouse.move(box.x + 40, box.y + 40);   // onto the child: still over the pad
  await page.waitForFunction(() => document.querySelector(".at")?.textContent === "40 40");
  await expect.textContains(page.locator(".left"), "0", "a child isn't leaving");
  await page.mouse.move(box.x + 250, box.y + 50);
  await page.waitForFunction(() => document.querySelector(".left")?.textContent === "1");
  await expect.textContains(page.locator(".at"), "-", "the hover is dropped on leaving");
}
