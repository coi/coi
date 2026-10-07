// fetch and websocket callbacks; refused ws fires error then close

// expected failures
export const expectedConsoleErrors = ["ws://127.0.0.1:1/", "status of 404"];

export async function run({ page, expect }) {
  await expect.textContains(page.locator(".fetched"), "success");
  await expect.textContains(page.locator(".missing"), "error");
  await page.waitForFunction(() => document.querySelector(".ws")?.textContent === "error close");
  // 1006: closed without a close frame
  await expect.textContains(page.locator(".code"), "1006");
  await expect.textContains(page.locator(".connected"), "closed");
}
