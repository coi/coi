// "\\${n}" is a backslash and then n; "\${n}" is the text ${n}, in both kinds of string

export async function run({ page, expect }) {
  const text = (sel) => page.locator(sel).innerText();
  await expect.ok((await text(".backslash")) === "a\\7b", `backslash: ${await text(".backslash")}`);
  await expect.ok((await text(".dollar")) === "a${n}b", `dollar: ${await text(".dollar")}`);
  await expect.ok((await text(".both")) === "\\${n}", `both: ${await text(".both")}`);
  await expect.ok((await text(".tmpl")) === "a${n}b", `template: ${await text(".tmpl")}`);
}
