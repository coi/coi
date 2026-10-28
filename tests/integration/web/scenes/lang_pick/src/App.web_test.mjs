// the language the app starts in: German for a German browser (lang/de.json fetched before the
// app mounts, so it never shows English first), English for a French one, and Lang.use's choice
// kept across a reload, over the browser's

export async function run({ page, expect }) {
  const url = page.url();
  const browser = page.context().browser();
  const open = async (locale) => {
    const p = await (await browser.newContext({ locale })).newPage();
    const seen = [];
    await p.exposeFunction("__seen", (t) => seen.push(t));
    await p.addInitScript(() => new MutationObserver(() => { const h = document.querySelector(".title"); if (h) window.__seen(h.textContent); })
      .observe(document, { childList: true, subtree: true, characterData: true }));
    await p.goto(url);
    await p.waitForSelector(".title");
    await p.waitForTimeout(200);
    return { p, seen };
  };

  const de = await open("de-DE");
  await expect.textContains(de.p.locator(".title"), "Einstellungen", "a German browser gets German");
  await expect.textContains(de.p.locator(".count"), "3 Notizbücher", "and its plurals");
  if (de.seen.includes("Settings")) throw new Error(`English showed first: ${de.seen}`);

  const fr = await open("fr-FR");
  await expect.textContains(fr.p.locator(".title"), "Settings", "no French file: English");
  await expect.textContains(fr.p.locator(".code"), "en", "and it says so");

  // the choice: kept, and the app starts again in it
  await fr.p.click(".de");
  await fr.p.waitForFunction(() => document.querySelector(".title")?.textContent === "Einstellungen");
  await fr.p.reload();
  await fr.p.waitForSelector(".title");
  await expect.textContains(fr.p.locator(".title"), "Einstellungen", "kept over the browser's French");
}
