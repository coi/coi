export async function run({ page, expect }) {
  const texts = async () => page.locator(".item").allInnerTexts();
  const classes = async () => page.locator(".item").evaluateAll((els) => els.map((e) => e.className));
  if ((await texts()).join(",") !== "1,2") throw new Error(`expected items 1,2 got ${await texts()}`);
  if ((await classes()).join(",") !== "item a,item a") throw new Error(`expected class a, got ${await classes()}`);

  await page.locator(".flip").click();
  await page.waitForFunction(() => document.querySelector(".item")?.className === "item b");
  if ((await texts()).join(",") !== "1,2") throw new Error(`items changed on flip: ${await texts()}`);

  await page.locator(".more").click();
  await page.waitForFunction(() => document.querySelectorAll(".item").length === 3);
  if ((await classes()).join(",") !== "item b,item b,item b") throw new Error(`new item has the wrong class: ${await classes()}`);

  await page.locator(".item").nth(2).click();
  await expect.textContains(page.locator(".picked"), "3");
}
