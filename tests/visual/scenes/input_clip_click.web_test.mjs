function intersect(a, b) {
  const x0 = Math.max(a.x, b.x);
  const y0 = Math.max(a.y, b.y);
  const x1 = Math.min(a.x + a.width, b.x + b.width);
  const y1 = Math.min(a.y + a.height, b.y + b.height);
  const w = x1 - x0;
  const h = y1 - y0;
  return { x0, y0, x1, y1, w, h };
}

export async function run({ page, expect }) {
  const status = page.locator(".status");
  await expect.textContains(status, "clip click test:");

  const panelA = page.locator(".panel.clip");
  const targetA = page.locator(".targetA");
  const panelBBox = await expect.bbox(panelA, "panelA bbox");
  const targetBBox = await expect.bbox(targetA, "targetA bbox");

  const yOutsidePanel = panelBBox.y + panelBBox.height + 16;
  await expect.ok(targetBBox.y + targetBBox.height > yOutsidePanel + 4, "targetA should overflow below panelA");

  const clickAX = targetBBox.x + targetBBox.width * 0.5;
  const clickAY = Math.min(targetBBox.y + targetBBox.height - 4, yOutsidePanel);
  await page.mouse.click(clickAX, clickAY);
  await page.waitForTimeout(50);
  await expect.textContains(status, "clipped=0");

  const insideI = intersect(panelBBox, targetBBox);
  await expect.ok(insideI.w > 8 && insideI.h > 8, "Expected targetA to overlap panelA");
  await page.mouse.click(insideI.x0 + insideI.w * 0.5, insideI.y0 + insideI.h * 0.5);
  await page.waitForFunction(() => document.querySelector(".status")?.textContent?.includes("clipped=1"));
  await expect.textContains(status, "clipped=1");

  const panelB = page.locator(".panel:not(.clip)");
  const targetB = page.locator(".targetB");
  const panelBB = await expect.bbox(panelB, "panelB bbox");
  const targetBB = await expect.bbox(targetB, "targetB bbox");
  const yOutsidePanelB = panelBB.y + panelBB.height + 16;
  await expect.ok(targetBB.y + targetBB.height > yOutsidePanelB + 4, "targetB should overflow below panelB");

  const clickBX = targetBB.x + targetBB.width * 0.5;
  const clickBY = Math.min(targetBB.y + targetBB.height - 4, yOutsidePanelB);
  await page.mouse.click(clickBX, clickBY);
  await page.waitForFunction(() => document.querySelector(".status")?.textContent?.includes("unclipped=1"));
  await expect.textContains(status, "unclipped=1");
}

