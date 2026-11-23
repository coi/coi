export async function run({ page, expect }) {
  const warnings = [];
  page.on("console", (m) => { if (m.type() === "warning" || m.type() === "error") warnings.push(m.text()); });
  const ids = () => page.locator(".list .box").evaluateAll((els) => els.map((e) => e.dataset.id).join(","));
  const marks = () => page.locator(".list .mark").count();
  await expect.ok((await ids()) === "1,2,3,4", `start: ${await ids()}`);

  // the boxes keep their elements: a sort moves them, it doesn't build them again
  await page.evaluate(() => document.querySelectorAll(".list .box").forEach((e) => { e.dataset.mark = "kept"; }));
  // plain sorts
  for (const w of ["2,3,4,1", "3,4,1,2"]) {
    await page.click(".raise");
    await expect.ok((await ids()) === w, `after raise: expected ${w}, got ${await ids()}`);
    await expect.ok((await marks()) === 4, `marks after plain raise: ${await marks()}`);
  }

  // a box with its <if> parts shut is the one raised, as a picked box is
  await page.click(".shut");
  await expect.ok((await marks()) === 3, `marks after shut: ${await marks()}`);
  await page.click(".raise");
  await expect.ok((await ids()) === "4,1,2,3", `after raising the shut box: ${await ids()}`);
  await expect.ok((await marks()) === 3, `marks after raising the shut box: ${await marks()}`);
  await page.click(".raise");
  await expect.ok((await ids()) === "1,2,3,4", `after another raise: ${await ids()}`);
  await page.click(".reopen");
  await expect.ok((await marks()) === 4, `marks after reopen: ${await marks()}`);
  await expect.ok((await page.locator(".list .tag").count()) === 4, `tags after reopen: ${await page.locator(".list .tag").count()}`);
  await page.click(".raise");
  await expect.ok((await ids()) === "2,3,4,1", `after the last raise: ${await ids()}`);
  const kept = await page.locator(".list .box").evaluateAll((els) => els.every((e) => e.dataset.mark === "kept"));
  await expect.ok(kept, "a box's element was built again by the sort");
  await expect.ok(warnings.length === 0, `console: ${warnings.slice(0, 4).join(" | ")}`);
}
