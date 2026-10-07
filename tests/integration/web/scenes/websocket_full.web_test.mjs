// minimal websocket server: "chat" subprotocol, echo, close 4001 "bye"
import http from "node:http";
import crypto from "node:crypto";

function startServer() {
  const server = http.createServer();
  server.on("upgrade", (req, socket) => {
    const accept = crypto.createHash("sha1")
      .update(req.headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest("base64");
    const offered = (req.headers["sec-websocket-protocol"] || "").split(",").map((s) => s.trim());
    const proto = offered.includes("chat") ? "Sec-WebSocket-Protocol: chat\r\n" : "";
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n${proto}\r\n`);
    const send = (opcode, payload) => {
      const head = payload.length < 126 ? Buffer.from([0x80 | opcode, payload.length])
                                        : Buffer.from([0x80 | opcode, 126, payload.length >> 8, payload.length & 255]);
      socket.write(Buffer.concat([head, payload]));
    };
    let buf = Buffer.alloc(0);
    socket.on("data", (chunk) => {
      buf = Buffer.concat([buf, chunk]);
      while (buf.length >= 6) {
        const opcode = buf[0] & 15;
        let len = buf[1] & 127, off = 2;
        if (len === 126) { len = buf.readUInt16BE(2); off = 4; }
        if (buf.length < off + 4 + len) return;
        const mask = buf.subarray(off, off + 4);
        const data = Buffer.from(buf.subarray(off + 4, off + 4 + len)).map((b, i) => b ^ mask[i % 4]);
        buf = buf.subarray(off + 4 + len);
        if (opcode === 1 && data.toString() === "close please") {
          const reason = Buffer.from("bye");
          const body = Buffer.alloc(2 + reason.length); body.writeUInt16BE(4001); reason.copy(body, 2);
          send(8, body);
        } else if (opcode === 1 || opcode === 2) send(opcode, data);
        else if (opcode === 8) socket.end();
      }
    });
    socket.on("error", () => {});
  });
  return new Promise((res) => server.listen(0, "127.0.0.1", () => res(server)));
}

export async function run({ page, expect }) {
  const server = await startServer();
  try {
    const url = new URL(page.url());
    url.searchParams.set("ws", `ws://127.0.0.1:${server.address().port}/`);
    await page.goto(url.href);
    await page.waitForFunction(() => document.querySelector(".closed")?.textContent !== "-", null, { timeout: 8000 });
    await expect.textContains(page.locator(".opened"), "true chat");
    await expect.textContains(page.locator(".echoed"), "hello");
    await expect.textContains(page.locator(".binary"), "3 bytes, sum 253");
    await expect.textContains(page.locator(".closed"), "4001 bye 1");
    await expect.textContains(page.locator(".after"), "not connected");
  } finally {
    server.close();
  }
}
