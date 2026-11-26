export async function run({ page }) {
  const drawn = async () => {
    await page.waitForFunction(() => (document.querySelector(".drawn")?.textContent || "").split(" ").length === 3);
    return page.locator(".drawn").innerText();
  };
  const first = await drawn();
  const [a, b, c] = first.split(" ").map(Number);
  if (!(a >= 0 && a < 1 && b >= 0 && b < 1 && c >= 0 && c < 1)) throw new Error(`outside [0, 1): ${first}`);
  if (a === b && b === c) throw new Error(`three draws alike: ${first}`);
  await page.reload();
  const second = await drawn();
  if (second === first) throw new Error(`the same numbers after a reload: ${first}`);
}
