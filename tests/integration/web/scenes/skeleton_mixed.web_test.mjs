// the cloned skeleton holds its dynamic parts in document order, and they keep updating

// the root's children in order; anchors (comments, empty text) left out
const shape = (page) => page.evaluate(() => [...document.getElementById("root").childNodes]
  .filter(n => n.nodeType === 1 || (n.nodeType === 3 && n.textContent !== ""))
  .map(n => n.nodeType === 3 ? "T:" + n.textContent : n.tagName + "." + n.className).join(","));
const text = (page, sel) => page.locator(sel).innerText();

export async function run({ page, expect }) {
  await page.waitForSelector("#root");
  let s = await shape(page);
  await expect.ok(s === "H2.title,P.even,SPAN.lead,EM.shown,B.badge,SPAN.mid,UL.list,SPAN.tail,T:0,T: tail-text ,BUTTON.,BUTTON.,BUTTON.", `at the start: ${s}`);
  const attrs = await page.evaluate(() => {
    const r = document.getElementById("root"), h = r.querySelector("h2");
    return [r.getAttribute("data-fixed"), r.hasAttribute("coi-scope"), h.hasAttribute("coi-scope"), h.textContent].join("|");
  });
  await expect.ok(attrs === "yes|true|true|Static title", `static attributes and text: ${attrs}`);
  await expect.ok((await text(page, "#root > p")) === "count 0", "the dynamic text");
  const rows = await page.evaluate(() => [...document.querySelectorAll(".item")].map(li => li.textContent + "/" + li.children[0].className).join(","));
  await expect.ok(rows === "1·/dot,2·/dot", `the rows: ${rows}`);

  await page.locator("#bump").click();
  s = await shape(page);
  await expect.ok(s.startsWith("H2.title,P.odd,") && s.includes("SPAN.tail,T:1,T: tail-text"), `after bump: ${s}`);
  await expect.ok((await text(page, "#root > p")) === "count 1", "the text follows");
  await expect.ok((await text(page, ".badge")) === "n=1", "the child component follows");

  await page.locator("#toggle").click();
  s = await shape(page);
  await expect.ok(s === "H2.title,P.odd,SPAN.lead,B.badge,SPAN.mid,UL.list,SPAN.tail,T:1,T: tail-text ,BUTTON.,BUTTON.,BUTTON.", `if off: ${s}`);
  await page.locator("#toggle").click();
  s = await shape(page);
  await expect.ok(s === "H2.title,P.odd,SPAN.lead,EM.shown,B.badge,SPAN.mid,UL.list,SPAN.tail,T:1,T: tail-text ,BUTTON.,BUTTON.,BUTTON.", `if on again: ${s}`);

  await page.locator("#more").click();
  const rows2 = await page.evaluate(() => [...document.querySelectorAll(".item")].map(li => li.textContent).join(","));
  await expect.ok(rows2 === "1·,2·,3·", `after more: ${rows2}`);
}
