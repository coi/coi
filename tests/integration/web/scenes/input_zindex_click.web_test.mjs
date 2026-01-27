function intersectionCenter(a, b) {
  const x0 = Math.max(a.x, b.x);
  const y0 = Math.max(a.y, b.y);
  const x1 = Math.min(a.x + a.width, b.x + b.width);
  const y1 = Math.min(a.y + a.height, b.y + b.height);
  const w = x1 - x0;
  const h = y1 - y0;
  return { x: x0 + w * 0.5, y: y0 + h * 0.5, w, h };
}

export async function run({ page, expect }) {
  const status = page.locator(".status");
  await expect.textContains(status, "clicks:");
  await expect.textContains(status, "B=0");

  const a = page.locator(".panel.a");
  const b = page.locator(".panel.b");
  const ba = await expect.bbox(a, "A bbox");
  const bb = await expect.bbox(b, "B bbox");
  const i = intersectionCenter(ba, bb);
  await expect.ok(i.w > 8 && i.h > 8, "Expected A and B to overlap");

  await page.mouse.click(i.x, i.y);
  await page.waitForFunction(() => document.querySelector(".status")?.textContent?.includes("B=1"));
  await expect.textContains(status, "B=1");
}

