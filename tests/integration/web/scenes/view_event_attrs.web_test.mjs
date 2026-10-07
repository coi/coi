// event attributes survive the if region being re-created

export async function run({ page, expect }) {
  const box = await page.locator(".surface").boundingBox();
  await page.mouse.move(box.x + 20, box.y + 30);
  await page.mouse.down();
  for (let i = 1; i <= 5; i++) await page.mouse.move(box.x + 20 + i * 20, box.y + 30);
  await page.mouse.up();
  await page.waitForFunction(() => document.querySelector(".drag")?.textContent.includes("up"));
  await expect.textContains(page.locator(".drag"), "down 20 up 120");
  const moves = parseInt(await page.locator(".moves").innerText(), 10);
  if (!(moves >= 5)) throw new Error(`expected at least 5 moves, got ${moves}`);

  await page.locator(".field").click();
  await expect.textContains(page.locator(".focus"), "focused");
  await page.locator(".flip").click();      // blur, region removed
  await expect.textContains(page.locator(".focus"), "blurred");
  await page.locator(".flip").click();      // region back
  await page.locator(".toggled").click();
  await page.waitForFunction(() => document.querySelector(".hits")?.textContent === "1");

  await page.locator(".item2").click();
  await expect.textContains(page.locator(".picked"), "2");
}
