"use strict";
const test = require("node:test"), assert = require("node:assert/strict");
const fs = require("node:fs"), path = require("node:path"), zlib = require("node:zlib");
const layout = require("../../main/web/badge/lightboard_layout.js");
const charset = require("../../main/web/badge/lightboard_charset.js");
const root = path.resolve(__dirname, "../..");
const modulePath = process.env.LIGHTBOARD_CANVAS_MODULE;
let canvas;
if (modulePath) {
    canvas = require(modulePath);
    assert.ok(canvas.GlobalFonts.registerFromPath(path.join(root, "main/web/badge/lightboard.woff2"), layout.FAMILY));
}
function crc32(bytes) {
    let value = 0xffffffff;
    for (const b of bytes) { value ^= b; for (let i = 0; i < 8; i++) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0); }
    return (value ^ 0xffffffff) >>> 0;
}
test("font coverage agrees with the verified source cmap for every BMP codepoint", () => {
    const source = JSON.parse(fs.readFileSync(path.join(root, "test/fixtures/lightboard/font-probe.json")));
    const set = new Set(source.codepoints);
    for (let cp = 0; cp < 65536; cp++) assert.equal(charset.supports(cp), set.has(cp));
    assert.equal(charset.supports(0x1f600), false);
});
test("offline layout/default templates use real loaded font metrics", { skip: !canvas }, () => {
    const ctx = canvas.createCanvas(1, 1).getContext("2d");
    assert.equal(layout.plan(ctx, "Hello 1024").mode, "poster");
    assert.equal(layout.plan(ctx, "VVVVV 你好").mode, "split");
    assert.equal(layout.plan(ctx, "W".repeat(20)).mode, "scroll");
    for (const [index, text] of layout.DEFAULT_TEXTS.entries()) {
        const rendered = layout.render(canvas.createCanvas, text, index);
        assert.ok(rendered.width <= 1536 && rendered.height <= 320);
        assert.deepEqual(rendered.layers, layout.render(canvas.createCanvas, text, index).layers);
        assert.equal(rendered.color, layout.COLORS[index]);
    }
});
test("every supported glyph's metrics and wide/tall extreme combinations fit the strip", { skip: !canvas }, () => {
    const ctx = canvas.createCanvas(1, 1).getContext("2d");
    let widest = "", tallest = "", maxWidth = 0, maxHeight = 0;
    for (let cp = 33; cp < 65536; cp++) if (charset.supports(cp)) {
        const char = String.fromCodePoint(cp), m = layout.measure(ctx, char, 64);
        if (m.width > maxWidth) { maxWidth = m.width; widest = char; }
        if (m.ascent + m.descent > maxHeight) { maxHeight = m.ascent + m.descent; tallest = char; }
    }
    for (const text of [widest.repeat(20), tallest.repeat(20), "W".repeat(20), "，".repeat(20), "中A1！".repeat(5)]) {
        const result = layout.render(canvas.createCanvas, text);
        assert.ok(result.width <= (result.mode === "scroll" ? 1536 : 240));
        assert.ok(result.height <= (result.mode === "scroll" ? 96 : 320));
    }
});
test("embedded default strip bundle restores the exact production A8 planes", { skip: !canvas }, () => {
    const bytes = fs.readFileSync(path.join(root, "main/resources/lightboard_defaults.bin"));
    assert.equal(bytes.readUInt32LE(0), 0x3144424c);
    assert.equal(bytes.readUInt16LE(4), 1); assert.equal(bytes.readUInt16LE(6), 6);
    assert.equal(bytes.readUInt32LE(8), bytes.length);
    assert.equal(bytes.readUInt32LE(12), crc32(bytes.subarray(16)));
    for (let index = 0; index < 6; index++) {
        const entry = bytes.subarray(16 + 96 * index), w = entry.readUInt16LE(2), h = entry.readUInt16LE(4);
        const text = entry.subarray(12, 12 + entry[0]).toString("utf8");
        const rendered = layout.render(canvas.createCanvas, text, index);
        assert.equal(w, rendered.width); assert.equal(h, rendered.height);
        for (let plane = 0; plane < 2; plane++) {
            const start = entry.readUInt32LE(72 + plane * 8), length = entry.readUInt32LE(76 + plane * 8);
            assert.ok(start >= 592 && start + length <= bytes.length);
            const packed = bytes.subarray(start, start + length), restored = Buffer.alloc(w * h);
            const count = Math.ceil(h / 4);
            assert.equal(packed.readUInt32LE(0), (count + 1) * 4);
            assert.equal(packed.readUInt32LE(count * 4), packed.length);
            for (let strip = 0; strip < count; strip++) {
                const compressed = packed.subarray(packed.readUInt32LE(strip * 4), packed.readUInt32LE((strip + 1) * 4));
                const data = zlib.inflateRawSync(compressed);
                assert.equal(data.length, Math.min(4, h - strip * 4) * w);
                for (let i = 0; i < data.length; i++) data[i] += i % w ? data[i - 1] : 0;
                data.copy(restored, strip * w * 4);
            }
            assert.deepEqual(restored, Buffer.from(rendered.layers[plane]));
        }
    }
});
