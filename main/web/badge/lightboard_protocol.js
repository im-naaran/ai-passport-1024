(function (root) {
    "use strict";
    const charset = typeof module !== "undefined" ? require("./lightboard_charset.js") : root.LightboardCharset;
    const encoder = new TextEncoder(), decoder = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true });
    const HEADER = 32, ENTRY = 96, MAX_BYTES = 0x1c0000 - 32;
    function crc32(bytes) {
        let crc = 0xffffffff;
        for (const b of bytes) { crc ^= b; for (let n = 0; n < 8; n++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0); }
        return (crc ^ 0xffffffff) >>> 0;
    }
    function validText(text) {
        return typeof text === "string" && text.length > 0 && text === text.trim() &&
            Array.from(text).length <= 20 && Array.from(text).every((c) => charset.supports(c.codePointAt(0)));
    }
    function normalize(input) {
        if (typeof input !== "string" || encoder.encode(input).length > 2048) throw new Error("文案输入过长");
        const texts = input.split("\n").map((s) => s.trim()).filter(Boolean);
        if (!texts.length || texts.length > 6) throw new Error("请填写 1～6 条文案");
        if (texts.some((s) => !validText(s))) throw new Error("每条最多 20 个字符，请使用支持的中文、英文、数字或标点");
        return texts;
    }
    function geometry(item) {
        const { width: w, height: h, stride, x, y, mode, fontSize } = item;
        if (![w, h, stride, x, y, fontSize].every(Number.isInteger) || !w || !h || stride !== w ||
            w < 0 || h < 0 || x < 0 || y < 0 || y + h > 320 || fontSize < 64 || fontSize > 104) return false;
        if (mode === 1) return w <= 1536 && h <= 96 && x === 0;
        return mode === 0 && w <= 240 && h <= 320 && x + w <= 240;
    }
    function encode(items, expectedRevision) {
        if (!Array.isArray(items) || !items.length || items.length > 6 || !Number.isInteger(expectedRevision) ||
            expectedRevision < 0 || expectedRevision > 0xffffffff) throw new Error("无效的配置版本或文案列表");
        const entries = items.map((item) => ({ ...item,
            mode: item.mode === "scroll" ? 1 : item.mode === "poster" || item.mode === "split" ? 0 : item.mode,
            fontSize: item.fontSize === undefined && item.layout ? item.layout.sizes[0] : item.fontSize }));
        let total = HEADER + entries.length * ENTRY;
        for (const item of entries) {
            if (!validText(item.text) || !geometry(item) || !Array.isArray(item.layers) || item.layers.length !== 2 ||
                item.layers.some((p) => !(p instanceof Uint8Array) || p.length !== item.width * item.height))
                throw new Error("文案或预览数据无效，请重新生成预览");
            total += item.width * item.height * 2;
        }
        if (total > MAX_BYTES) throw new Error("文案资源超出设备容量");
        const bytes = new Uint8Array(total), v = new DataView(bytes.buffer);
        v.setUint32(0, 0x3150424c, true); v.setUint16(4, 1, true); bytes[6] = entries.length; bytes[7] = 1;
        v.setUint16(8, 1, true); v.setUint32(12, total, true); v.setUint32(16, expectedRevision, true);
        let offset = HEADER + entries.length * ENTRY;
        entries.forEach((item, index) => {
            const at = HEADER + index * ENTRY, text = encoder.encode(item.text);
            bytes[at] = text.length; bytes[at + 1] = item.mode;
            v.setUint16(at + 2, item.width, true); v.setUint16(at + 4, item.height, true);
            v.setUint16(at + 6, item.stride, true); v.setInt16(at + 8, item.x, true); v.setInt16(at + 10, item.y, true);
            bytes.set(text, at + 12); v.setUint16(at + 88, item.fontSize, true);
            item.layers.forEach((data, layer) => {
                v.setUint32(at + 72 + layer * 8, offset, true); v.setUint32(at + 76 + layer * 8, data.length, true);
                bytes.set(data, offset); offset += data.length;
            });
        });
        v.setUint32(20, crc32(bytes.subarray(HEADER)), true);
        v.setUint32(24, crc32(bytes.subarray(0, 24)), true);
        return bytes;
    }
    function decode(bytes) {
        if (!(bytes instanceof Uint8Array) || bytes.length < HEADER || bytes.length > MAX_BYTES) throw new Error("Invalid bundle size");
        const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength), count = bytes[6];
        if (v.getUint32(0, true) !== 0x3150424c || v.getUint16(4, true) !== 1 || !count || count > 6 || bytes[7] !== 1 ||
            v.getUint16(8, true) !== 1 || v.getUint16(10, true) || v.getUint32(28, true) ||
            v.getUint32(12, true) !== bytes.length || crc32(bytes.subarray(0, 24)) !== v.getUint32(24, true) ||
            crc32(bytes.subarray(HEADER)) !== v.getUint32(20, true)) throw new Error("Invalid bundle header or checksum");
        let offset = HEADER + count * ENTRY;
        if (offset > bytes.length) throw new Error("Truncated directory");
        const items = [];
        for (let index = 0; index < count; index++) {
            const at = HEADER + index * ENTRY, length = bytes[at];
            if (!length || length > 60 || bytes.subarray(at + 12 + length, at + 72).some(Boolean) ||
                bytes.subarray(at + 90, at + 96).some(Boolean)) throw new Error("Invalid text padding");
            const item = { text: decoder.decode(bytes.subarray(at + 12, at + 12 + length)), mode: bytes[at + 1],
                width: v.getUint16(at + 2, true), height: v.getUint16(at + 4, true), stride: v.getUint16(at + 6, true),
                x: v.getInt16(at + 8, true), y: v.getInt16(at + 10, true), fontSize: v.getUint16(at + 88, true), layers: [] };
            if (!validText(item.text) || !geometry(item)) throw new Error("Invalid text or geometry");
            for (let layer = 0; layer < 2; layer++) {
                const start = v.getUint32(at + 72 + layer * 8, true), length = v.getUint32(at + 76 + layer * 8, true);
                if (start !== offset || length !== item.width * item.height || offset + length > bytes.length)
                    throw new Error("Invalid plane bounds");
                item.layers.push(bytes.subarray(offset, offset + length)); offset += length;
            }
            items.push(item);
        }
        if (offset !== bytes.length) throw new Error("Trailing data");
        return { expectedRevision: v.getUint32(16, true), items };
    }
    const api = { normalize, validText, geometry, crc32, encode, decode, HEADER, ENTRY, MAX_BYTES };
    root.LightboardProtocol = api;
    if (typeof module !== "undefined") module.exports = api;
})(globalThis);
