// "burst N S": N text messages of S bytes
import http from "node:http";
import crypto from "node:crypto";

function startServer() {
  const server = http.createServer();
  server.on("upgrade", (req, socket) => {
    const accept = crypto.createHash("sha1")
      .update(req.headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest("base64");
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    const send = (payload) => {
      const head = payload.length < 126 ? Buffer.from([0x81, payload.length])
                                        : Buffer.from([0x81, 126, payload.length >> 8, payload.length & 255]);
      socket.write(Buffer.concat([head, payload]));
    };
    let buf = Buffer.alloc(0);
    socket.on("data", (chunk) => {
      buf = Buffer.concat([buf, chunk]);
      while (buf.length >= 6) {
        let len = buf[1] & 127, off = 2;
        if (len === 126) { len = buf.readUInt16BE(2); off = 4; }
        if (buf.length < off + 4 + len) return;
        const mask = buf.subarray(off, off + 4);
        const text = Buffer.from(buf.subarray(off + 4, off + 4 + len)).map((b, i) => b ^ mask[i % 4]).toString();
        buf = buf.subarray(off + 4 + len);
        const m = /^burst (\d+) (\d+)$/.exec(text);
        if (m) for (let i = 0; i < +m[1]; i++) send(Buffer.alloc(+m[2], 120));
      }
    });
    socket.on("error", () => {});
  });
  return new Promise((res) => server.listen(0, "127.0.0.1", () => res(server)));
}

// polls from node, the page has no rAF or timers in parts of this test
async function waitFor(page, check, what) {
  for (let i = 0; i < 100; i++) {
    if (await page.evaluate(check)) return;
    await new Promise((r) => setTimeout(r, 50));
  }
  const got = await page.evaluate(() => `${document.querySelector(".count").textContent} / ${document.querySelector(".bytes").textContent}`);
  throw new Error(`${what}: got count/bytes ${got}`);
}


export async function run({ page }) {
  const server = await startServer();
  const dropped = [];
  page.on("console", (m) => { if (m.text().includes("Event buffer full")) dropped.push(m.text()); });
  try {
    const url = new URL(page.url());
    url.searchParams.set("ws", `ws://127.0.0.1:${server.address().port}/`);
    await page.goto(url.href);
    await page.locator(".connect").waitFor();

    // hidden tab: no rAF
    await page.evaluate(() => {
      window.__raf = window.requestAnimationFrame;
      window.requestAnimationFrame = () => 0;
      Object.defineProperty(document, "hidden", { configurable: true, get: () => true });
      document.dispatchEvent(new Event("visibilitychange"));
    });
    await page.locator(".connect").click();
    await waitFor(page, () => +document.querySelector(".count").textContent === 3, "small burst while hidden");

    // no timers either: a burst past the 1MB event buffer has to drain, not drop
    await page.evaluate(() => { window.__st = window.setTimeout; window.setTimeout = () => 0; });
    await page.locator(".burst").click();
    await waitFor(page, () => +document.querySelector(".count").textContent >= 3 + 16, "drain on full buffer");

    await page.evaluate(() => {
      window.setTimeout = window.__st;
      window.requestAnimationFrame = window.__raf;
      delete document.hidden;
    });
    await page.locator(".poke").click();
    await waitFor(page, () => +document.querySelector(".count").textContent === 33, "rest after visible");
    const bytes = await page.locator(".bytes").textContent();
    if (bytes.trim() !== String(30 + 30 * 60000)) throw new Error(`bytes: ${bytes}`);
    if (dropped.length) throw new Error(`dropped events: ${dropped[0]}`);
  } finally {
    server.close();
  }
}
