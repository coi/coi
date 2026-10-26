// PointerFlags.Hold: a finger held still is HELD and then drags without scrolling the list,
// and its release isn't a click; a quick swipe scrolls as before and a tap clicks

export async function run({ page, expect }) {
  const cdp = await page.context().newCDPSession(page);
  await cdp.send("Emulation.setTouchEmulationEnabled", { enabled: true, maxTouchPoints: 5 });
  const touch = (type, x, y) => cdp.send("Input.dispatchTouchEvent", { type, touchPoints: type === "touchEnd" ? [] : [{ x, y, id: 1 }] });
  const box = await page.locator(".list").boundingBox();
  const x = box.x + 40;
  const scrollTop = () => page.locator(".list").evaluate((el) => el.scrollTop);

  // held still on row 2, then moved down: HELD at the finger, moves arrive, the list stays put
  await touch("touchStart", x, box.y + 60);
  await page.waitForTimeout(700);
  await page.waitForFunction(() => document.querySelector(".held")?.textContent.startsWith("2 "));
  await expect.textContains(page.locator(".held"), "2 20", "held where the finger is, in the row");
  for (let i = 1; i <= 10; i++) { await touch("touchMove", x, box.y + 60 - i * 6); await page.waitForTimeout(16); }
  await touch("touchEnd");
  await page.waitForTimeout(200);
  const moves = parseInt(await page.locator(".dragmoves").innerText(), 10);
  if (!(moves >= 5)) throw new Error(`the held finger's moves arrive, got ${moves}`);
  if ((await scrollTop()) !== 0) throw new Error(`the list didn't scroll under a held finger: ${await scrollTop()}`);
  await expect.textContains(page.locator(".clicks"), "0", "no click after a hold");

  // a tap is still a click
  await touch("touchStart", x, box.y + 20);
  await page.waitForTimeout(50);
  await touch("touchEnd");
  await page.waitForFunction(() => document.querySelector(".clicks")?.textContent === "1");

  // a quick swipe scrolls, and isn't held
  await touch("touchStart", x, box.y + 100);
  for (let i = 1; i <= 10; i++) { await touch("touchMove", x, box.y + 100 - i * 8); await page.waitForTimeout(16); }
  await touch("touchEnd");
  await page.waitForTimeout(700);
  if (!((await scrollTop()) > 20)) throw new Error(`a swipe scrolls the list: ${await scrollTop()}`);
  await expect.textContains(page.locator(".held"), "2 20", "a swipe isn't a hold");
}
