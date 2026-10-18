"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

require("../../main/web/badge/badge_image.js");
require("../../main/web/badge/app.js");

const image = globalThis.BadgeImage;
const app = globalThis.BadgeAppLogic;

test("bio validation uses UTF-8 bytes and permits empty text", () => {
    assert.equal(app.validBio(""), true);
    assert.equal(app.bioBytes("保持好奇").length, 12);
    assert.equal(app.validBio("a".repeat(96)), true);
    assert.equal(app.validBio("a".repeat(97)), false);
    assert.equal(app.validBio("a\nb"), false);
});

test("version 3 envelope stores name bio shape and fixed image length", () => {
    const pixels = new Uint8Array(80000);
    for (const [shape, value] of [["square", 0], ["rounded", 1], ["circle", 2]]) {
        const body = app.buildEnvelope(" 张三 ", " 保持好奇 ", pixels, shape);
        const view = new DataView(body.buffer);
        assert.equal(view.getUint32(0, true), 0x46525042);
        assert.equal(view.getUint16(4, true), 3);
        assert.equal(view.getUint16(6, true), 6);
        assert.equal(view.getUint16(8, true), 12);
        assert.equal(view.getUint8(10), value);
        assert.equal(view.getUint8(11), 0);
        assert.equal(view.getUint32(12, true), 80000);
        assert.equal(body.length, 16 + 6 + 12 + 80000);
    }
    assert.throws(() => app.buildEnvelope("张三", "", pixels, "unknown"), /不兼容/);
});

test("save state includes optional bio validation", () => {
    const valid = { hasImage: true, name: "张三", bio: "", uploading: false,
        shape: "square", sourceShape: null };
    assert.equal(app.saveDisabled(valid), false);
    assert.equal(app.saveDisabled({ ...valid, bio: "a".repeat(97) }), true);
    assert.equal(app.saveDisabled({ ...valid, shape: "unknown" }), true);
});

test("profile shape parsing is strict and supports radio initialization values", () => {
    for (const shape of ["square", "rounded", "circle"])
        assert.equal(app.normalizeProfileShape(shape), shape);
    assert.throws(() => app.normalizeProfileShape(undefined), /不兼容/);
    assert.throws(() => app.normalizeProfileShape("unknown"), /不兼容/);
});

test("device-reflected masks cannot expand until a local original is selected", () => {
    assert.equal(app.shapeExpansionBlocked("circle", "rounded"), true);
    assert.equal(app.shapeExpansionBlocked("circle", "square"), true);
    assert.equal(app.shapeExpansionBlocked("rounded", "square"), true);
    assert.equal(app.shapeExpansionBlocked("square", "rounded"), false);
    assert.equal(app.shapeExpansionBlocked("square", "circle"), false);
    assert.equal(app.shapeExpansionBlocked("rounded", "circle"), false);
    assert.equal(app.shapeExpansionBlocked("circle", "circle"), false);
    assert.equal(app.shapeExpansionBlocked(null, "square"), false);
    assert.equal(app.saveDisabled({ hasImage: true, name: "张三", bio: "", uploading: false,
        shape: "square", sourceShape: "circle" }), true);
});

test("shape geometry covers square rounded and circle boundaries", () => {
    assert.equal(image.normalizedShape("unknown"), "square");
    assert.equal(image.shapeContains("square", 0, 0), true);
    assert.equal(image.shapeContains("rounded", 0, 0), false);
    assert.equal(image.shapeContains("rounded", 24, 0), true);
    assert.equal(image.shapeContains("circle", 0, 0), false);
    assert.equal(image.shapeContains("circle", 100, 100), true);
    assert.equal(image.ROUNDED_RADIUS, 24);
});

test("RGB565 conversion remains fixed at 80000 bytes", () => {
    const rgba = new Uint8ClampedArray(200 * 200 * 4);
    rgba.fill(255);
    assert.equal(image.rgbaToRgb565(rgba, 200 * 200).length, 80000);
});

test("rectangular cover geometry handles rotation zoom and pan bounds", () => {
    const portrait = image.coverGeometry(600, 800,
        { rotation: 0, zoom: 1, panX: 999, panY: -999 }, 240, 320);
    assert.equal(portrait.scale, 0.4);
    assert.equal(portrait.renderedWidth, 240);
    assert.equal(portrait.renderedHeight, 320);
    assert.equal(portrait.panX, 0);
    assert.equal(portrait.panY, 0);

    const rotated = image.coverGeometry(1200, 400,
        { rotation: 90, zoom: 2, panX: 999, panY: -999 }, 240, 320);
    assert.equal(rotated.rotation, 90);
    assert.equal(rotated.renderedWidth, 480);
    assert.equal(rotated.renderedHeight, 1440);
    assert.equal(rotated.panX, 120);
    assert.equal(rotated.panY, -560);
});

test("extreme aspect ratios remain covered inside a 240 by 320 canvas", () => {
    for (const [width, height] of [[4000, 100], [100, 4000]]) {
        const geometry = image.coverGeometry(width, height,
            { rotation: 0, zoom: 1, panX: 1e6, panY: 1e6 }, 240, 320);
        assert.ok(geometry.renderedWidth >= 240);
        assert.ok(geometry.renderedHeight >= 320);
        assert.ok(Math.abs(geometry.panX) <= geometry.maxPanX);
        assert.ok(Math.abs(geometry.panY) <= geometry.maxPanY);
    }
});

test("pointer deltas scale from CSS pixels to backing canvas pixels", () => {
    assert.deepEqual(image.canvasPointerDelta(12, -16, 240, 320, 120, 160),
        { x: 24, y: -32 });
    assert.throws(() => image.canvasPointerDelta(1, 1, 240, 320, 0, 160), RangeError);
});

test("240 by 320 RGB565 output has fixed length and round-trips quantized pixels", () => {
    const rgba = new Uint8ClampedArray(240 * 320 * 4);
    for (let index = 0; index < rgba.length; index += 4) {
        rgba[index] = 255;
        rgba[index + 1] = 128;
        rgba[index + 2] = 0;
        rgba[index + 3] = 255;
    }
    const bytes = image.rgbaToRgb565(rgba, 240 * 320);
    assert.equal(bytes.length, 153600);
    const decoded = image.rgb565ToImageData(bytes, 240, 320, null);
    assert.equal(decoded.data.length, 240 * 320 * 4);
    assert.deepEqual(Array.from(decoded.data.slice(0, 4)), [255, 130, 0, 255]);
});

test("transparent mask pixels match the device canvas instead of white", () => {
    const transparent = new Uint8ClampedArray([255, 255, 255, 0]);
    const rgb565 = image.rgbaToRgb565(transparent, 1);
    assert.deepEqual(image.DEVICE_CANVAS_RGB, [0x0b, 0x0f, 0x0e]);
    assert.deepEqual(Array.from(rgb565), [0x61, 0x08]);
});

test("profile form asks for name before bio", () => {
    const html = fs.readFileSync(path.join(__dirname, "../../main/web/badge/index.html"), "utf8");
    assert.ok(html.indexOf('id="name"') < html.indexOf('id="bio"'));
});

test("web assets remain offline", () => {
    const files = ["index.html", "style.css", "app.js", "badge_image.js"].map((name) =>
        fs.readFileSync(path.join(__dirname, `../../main/web/badge/${name}`), "utf8"));
    for (const source of files) assert.doesNotMatch(source, /(?:src|href|fetch)\s*=\s*["']https?:/i);
});

test("embedded page cache-busts all static resources", () => {
    const html = fs.readFileSync(path.join(__dirname, "../../main/web/badge/index.html"), "utf8");
    assert.match(html, /Cache-Control[^>]+no-store/);
    assert.match(html, /href="\/style\.css\?v=\d+"/);
    assert.match(html, /src="\/badge_image\.js\?v=\d+"/);
    assert.match(html, /src="\/app\.js\?v=\d+"/);
});

test("transparent crop area is not painted by the preview stylesheet", () => {
    const css = fs.readFileSync(path.join(__dirname, "../../main/web/badge/style.css"), "utf8");
    const canvasRule = css.match(/canvas\s*\{([^}]*)\}/);
    assert.ok(canvasRule);
    assert.match(canvasRule[1], /background:\s*transparent/);
    assert.doesNotMatch(canvasRule[1], /box-shadow/);
});


test("profile editor retains photo controls without gallery entry or requests", () => {
    const dir = path.join(__dirname, "../../main/web/badge");
    const html = fs.readFileSync(path.join(dir, "index.html"), "utf8");
    const js = fs.readFileSync(path.join(dir, "app.js"), "utf8");
    for (const id of ["profile-form", "preview", "photo", "zoom", "rotate", "name", "bio", "save"])
        assert.ok(html.includes(`id="${id}"`));
    assert.doesNotMatch(html, /personalization|data-slot|个性化图片/);
    assert.doesNotMatch(js, /personalization|customCanvas|customForm/);
});

test("profile-only page initializes and saves with its actual remaining DOM controls", async () => {
    const dir = path.join(__dirname, "../../main/web/badge");
    const html = fs.readFileSync(path.join(dir, "index.html"), "utf8");
    const nodes = new Map();
    const canvasContext = {
        putImageData() {},
        getImageData: () => ({ data: new Uint8ClampedArray(200 * 200 * 4) }),
    };
    function node() {
        return { value: "", disabled: false, dataset: {}, textContent: "",
            classList: { toggle() {} }, handlers: {},
            addEventListener(type, handler) { this.handlers[type] = handler; },
            getContext: () => canvasContext };
    }
    // Only IDs present in the real page are available: stale gallery selectors fail initialization.
    for (const match of html.matchAll(/id="([^"]+)"/g)) nodes.set(`#${match[1]}`, node());
    const shapes = ["square", "rounded", "circle"].map((value) => ({ ...node(), value }));
    const requests = [];
    let rejectSave = false;
    const sandbox = { TextEncoder, Uint8Array, DataView,
        BadgeImage: { ...image, renderCrop: () => ({}) },
        document: {
            querySelector: (selector) => nodes.get(selector) || null,
            querySelectorAll: () => shapes,
            createElement: () => node(),
        },
        async fetch(url, options = {}) {
            requests.push({ url, options });
            if (options.method === "POST") {
                assert.equal(nodes.get("#save").disabled, true);
                return { ok: !rejectSave, status: rejectSave ? 500 : 200,
                    json: async () => rejectSave ? { error: "storage_error" } : { ok: true } };
            }
            if (url === "/api/status") return { ok: true,
                json: async () => ({ token: "0000000012345678", connected: true }) };
            if (url === "/api/profile") return { ok: true,
                json: async () => ({ name: "张三", bio: "保持好奇", shape: "square" }) };
            if (url === "/api/photo") return { ok: true,
                arrayBuffer: async () => new Uint8Array(80000).buffer };
            throw new Error(`Unexpected request: ${url}`);
        },
    };
    vm.runInNewContext(fs.readFileSync(path.join(dir, "app.js"), "utf8"), sandbox);
    await new Promise(setImmediate);
    assert.deepEqual(requests.map((r) => r.url), ["/api/status", "/api/profile", "/api/photo"]);
    assert.equal(nodes.get("#name").value, "张三");
    assert.equal(nodes.get("#bio").value, "保持好奇");
    assert.equal(nodes.get("#connection").textContent, "手机已连接");
    assert.equal(nodes.get("#save").disabled, false);

    nodes.get("#name").value = "";
    nodes.get("#name").handlers.input();
    assert.equal(nodes.get("#save").disabled, true);
    nodes.get("#name").value = "李四";
    nodes.get("#name").handlers.input();
    await nodes.get("#profile-form").handlers.submit({ preventDefault() {} });
    const saved = requests.at(-1);
    assert.equal(saved.url, "/api/profile");
    assert.equal(saved.options.headers["X-Passport-Session"], "0000000012345678");
    assert.equal(saved.options.body.length, 80034);
    assert.equal(nodes.get("#message").dataset.kind, "success");
    assert.equal(nodes.get("#save").disabled, false);

    rejectSave = true;
    await nodes.get("#profile-form").handlers.submit({ preventDefault() {} });
    assert.equal(nodes.get("#message").dataset.kind, "error");
    assert.match(nodes.get("#message").textContent, /原工牌资料仍会保留/);
    assert.equal(nodes.get("#name").value, "李四");
    assert.equal(nodes.get("#save").disabled, false);
});
