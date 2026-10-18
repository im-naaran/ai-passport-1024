"use strict";
// Dev-only Canvas dependency is supplied explicitly; production uses native browser Canvas.
const fs = require("node:fs"), path = require("node:path"), zlib = require("node:zlib");
const crypto = require("node:crypto");
const [modulePath, output = "main/resources/lightboard_defaults.bin"] = process.argv.slice(2);
if (!modulePath) throw new Error("Pass the installed @napi-rs/canvas module directory");
const { createCanvas, GlobalFonts } = require(path.resolve(modulePath));
const layout = require("../main/web/badge/lightboard_layout.js");
const font = path.resolve("main/web/badge/lightboard.woff2");
if (!GlobalFonts.registerFromPath(font, layout.FAMILY)) throw new Error("Font failed to load");

function crc32(bytes) {
    let crc = 0xffffffff;
    for (const b of bytes) { crc ^= b; for (let i = 0; i < 8; i++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0); }
    return (crc ^ 0xffffffff) >>> 0;
}
function pack(alpha, width, height) {
    const count = Math.ceil(height / 4), table = Buffer.alloc(4 * (count + 1)), strips = [];
    let offset = table.length;
    for (let y = 0; y < height; y += 4) {
        const original = alpha.subarray(y * width, Math.min(y + 4, height) * width);
        const filtered = Buffer.alloc(original.length);
        for (let i = 0; i < original.length; i++) filtered[i] = original[i] - (i % width ? original[i - 1] : 0);
        const compressed = zlib.deflateRawSync(filtered, { level: 9 });
        const restored = zlib.inflateRawSync(compressed);
        for (let i = 0; i < restored.length; i++) restored[i] += i % width ? restored[i - 1] : 0;
        if (!Buffer.from(original).equals(restored)) throw new Error("Lossless check failed");
        table.writeUInt32LE(offset, y / 4 * 4); strips.push(compressed); offset += compressed.length;
    }
    table.writeUInt32LE(offset, count * 4);
    return Buffer.concat([table, ...strips]);
}
const items = layout.DEFAULT_TEXTS.map((text, i) => layout.render(createCanvas, text, i));
const header = Buffer.alloc(16), directory = Buffer.alloc(items.length * 96), planes = [], report = [];
let offset = header.length + directory.length;
for (const [index, item] of items.entries()) {
    const entry = directory.subarray(index * 96, (index + 1) * 96), text = Buffer.from(item.text);
    if (text.length > 60) throw new Error("Default text exceeds directory");
    entry[0] = text.length; entry[1] = item.mode === "scroll" ? 1 : 0;
    entry.writeUInt16LE(item.width, 2); entry.writeUInt16LE(item.height, 4); entry.writeUInt16LE(item.stride, 6);
    entry.writeInt16LE(item.x, 8); entry.writeInt16LE(item.y, 10); text.copy(entry, 12);
    for (let layer = 0; layer < 2; layer++) {
        const packed = pack(item.layers[layer], item.width, item.height);
        entry.writeUInt32LE(offset, 72 + layer * 8); entry.writeUInt32LE(packed.length, 76 + layer * 8);
        planes.push(packed); offset += packed.length;
    }
    entry.writeUInt16LE(item.layout.sizes[0], 88);
    report.push({ text: item.text, mode: item.mode, width: item.width, height: item.height,
        rawBytes: item.width * item.height * 2 });
}
header.writeUInt32LE(0x3144424c, 0); header.writeUInt16LE(1, 4); header.writeUInt16LE(items.length, 6);
header.writeUInt32LE(offset, 8);
const payload = Buffer.concat([directory, ...planes]); header.writeUInt32LE(crc32(payload), 12);
const result = Buffer.concat([header, payload]);
fs.mkdirSync(path.dirname(output), { recursive: true }); fs.writeFileSync(output, result);
const record = { sha256: crypto.createHash("sha256").update(result).digest("hex"), bytes: result.length,
    maxStripBytes: Math.max(...items.map((item) => item.width * 4)), items: report };
fs.writeFileSync(output + ".json", JSON.stringify(record, null, 2) + "\n");
console.log(JSON.stringify(record, null, 2));
