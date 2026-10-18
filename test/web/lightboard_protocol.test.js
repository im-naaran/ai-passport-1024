"use strict";
const test = require("node:test"), assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const p = require("../../main/web/badge/lightboard_protocol.js");
const fixtures = path.resolve(__dirname, "../fixtures/lightboard");
const vectors = JSON.parse(fs.readFileSync(path.join(fixtures, "text-vectors.json")));
function item(text = "Hello 1024") { return { text, mode: 0, width: 8, height: 4, stride: 8, x: 12, y: 100,
    fontSize: 64, layers: [Uint8Array.from({ length: 32 }, (_, i) => i), new Uint8Array(32).fill(255)] }; }
function seal(bytes) {
    const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    v.setUint32(20, p.crc32(bytes.subarray(32)), true); v.setUint32(24, p.crc32(bytes.subarray(0, 24)), true);
}
test("C/JS shared line vectors count Unicode characters and retain order", () => {
    for (const v of vectors) {
        if (v.texts) assert.deepEqual(p.normalize(v.input), v.texts, v.id);
        else assert.throws(() => p.normalize(v.input), undefined, v.id);
    }
});
test("little-endian bundle is identical to the C golden vector and decodes subarrays", () => {
    const encoded = p.encode([item(), item("中A1！")], 7);
    assert.deepEqual(Buffer.from(encoded), fs.readFileSync(path.join(fixtures, "bundle.bin")));
    const wrapped = new Uint8Array(encoded.length + 20); wrapped.set(encoded, 7);
    const parsed = p.decode(wrapped.subarray(7, 7 + encoded.length));
    assert.equal(parsed.expectedRevision, 7); assert.equal(parsed.items[1].text, "中A1！");
    assert.deepEqual(parsed.items[0].layers[1], item().layers[1]);
});
test("invalid versions, reserved bytes, UTF-8, geometry and bounds are rejected beyond CRC", () => {
    const original = p.encode([item()], 0);
    const edits = [b => b[4]++, b => b[7]++, b => b[8]++, b => b[10]++, b => b[28]++,
        b => b[32] = 61, b => b[33] = 2, b => b[34] = 0, b => b[38]++, b => b[40] = 255,
        b => b[44] = 0xff, b => b[32 + 72]++, b => b[32 + 76]++, b => b[32 + 90]++];
    for (const edit of edits) { const bytes = original.slice(); edit(bytes); seal(bytes); assert.throws(() => p.decode(bytes)); }
    const damaged = original.slice(); damaged[damaged.length - 1] ^= 1; assert.throws(() => p.decode(damaged));
    assert.throws(() => p.decode(original.subarray(0, original.length - 1)));
    assert.throws(() => p.encode([item("a".repeat(21))], 0));
    assert.throws(() => p.encode(Array.from({ length: 7 }, () => item()), 0));
    assert.throws(() => p.encode([item()], -1));
});
test("six maximal raw planes fit a bank without overlap or integer wrapping", () => {
    const maximal = { ...item("中".repeat(20)), mode: 1, width: 1536, stride: 1536, height: 96, x: 0,
        layers: [new Uint8Array(1536 * 96), new Uint8Array(1536 * 96)] };
    const bytes = p.encode(Array.from({ length: 6 }, () => maximal), 0xffffffff);
    assert.ok(bytes.length <= p.MAX_BYTES); assert.equal(bytes.length, 1770080);
    assert.equal(p.decode(bytes).items.length, 6);
});
