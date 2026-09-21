// web/mock/wsframe.ts
// The minimum of RFC 6455 the mock needs: the Sec-WebSocket-Accept computation for the
// handshake, and server->client text-frame encoding (unmasked, per spec servers must not mask).
// We never decode client frames: the SPA is push-only, so any inbound bytes are ignored.
import { createHash, randomBytes } from "node:crypto";

const WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

export function acceptKey(clientKey: string): string {
  return createHash("sha1").update(clientKey + WS_GUID).digest("base64");
}

// Encode a UTF-8 string as a single unmasked text frame (opcode 0x1, FIN set).
export function encodeText(text: string): Buffer {
  const payload = Buffer.from(text, "utf8");
  const len = payload.length;
  let header: Buffer;
  if (len < 126) {
    header = Buffer.from([0x81, len]);
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x81; header[1] = 126; header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x81; header[1] = 127; header.writeBigUInt64BE(BigInt(len), 2);
  }
  return Buffer.concat([header, payload]);
}

export function randomTicket(): string { return randomBytes(16).toString("hex"); } // 32 hex chars
