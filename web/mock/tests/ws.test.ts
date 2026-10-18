// web/mock/tests/ws.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { TicketStore, stateFrame, deltaFrame, pingFrame, diffValues } from "../ws.ts";
import { acceptKey, encodeText } from "../wsframe.ts";

const store = new TicketStore();
const t = store.issue(1000);
ok(/^[0-9a-f]{32}$/.test(t.ticket), "ticket is 32 hex chars");
eq(t.ttl_ms, 30000, "ttl is 30000");
eq(store.redeem(t.ticket, 2000), true, "fresh ticket redeems");
eq(store.redeem(t.ticket, 2000), false, "ticket is single-use");
eq(store.redeem("deadbeef".repeat(4), 40000), false, "unknown ticket rejected");
const t2 = store.issue(0);
eq(store.redeem(t2.ticket, 40000), false, "expired ticket rejected");

// RFC 6455 known-answer for the accept key.
eq(acceptKey("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "accept key matches RFC example");

const f = JSON.parse(stateFrame({ ch_enable: true }));
eq(f.type, "state", "state frame type");
eq(f.values.ch_enable, true, "state frame carries values");
eq(JSON.parse(deltaFrame({ flow: 55 })).type, "delta", "delta frame type");
eq(JSON.parse(pingFrame()).type, "ping", "ping frame type");

// diffValues returns only changed keys.
const d = diffValues({ a: 1, b: 2 }, { a: 1, b: 3 });
eq(JSON.stringify(d), JSON.stringify({ b: 3 }), "diff returns only changed keys");

// short text frame header: 0x81 opcode, unmasked, length in byte 2.
const buf = encodeText("hi");
eq(buf[0], 0x81, "fin+text opcode");
eq(buf[1], 2, "unmasked length 2");
eq(buf.subarray(2).toString("utf8"), "hi", "payload preserved");

// medium frame: 200 bytes uses extended 16-bit length (byte1 = 126).
const big = encodeText("x".repeat(200));
eq(big[1], 126, "extended length marker for 200 bytes");

report("mock/ws");
