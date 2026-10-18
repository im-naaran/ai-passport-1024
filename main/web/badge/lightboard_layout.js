(function (root) {
    "use strict";
    const FAMILY = "PassportLightboard";
    const COLORS = ["#ff3dbb", "#ff8a24", "#19ddff", "#8dff39", "#b46cff", "#fff143"];
    const DEFAULT_TEXTS = ["需求不改，指定能橙", "今天不修 BUG", "禁止周五上线", "Hello 1024",
        "代码无 BUG，生活有 LOVE", "编译通过，万事顺遂"];

    async function loadFont() {
        // Font bytes belong to this device; never use system fallback or a CDN.
        const face = await new FontFace(FAMILY, "url(/lightboard.woff2)").load();
        document.fonts.add(face);
        return face;
    }

    function measure(ctx, text, size) {
        ctx.font = `${size}px ${FAMILY}`;
        let width = 0, ascent = 0, descent = 0, left = 0, right = 0;
        for (const char of text) {
            const m = ctx.measureText(char);
            left = Math.min(left, width - m.actualBoundingBoxLeft);
            right = Math.max(right, width + m.actualBoundingBoxRight);
            width += m.width;
            ascent = Math.max(ascent, m.actualBoundingBoxAscent);
            descent = Math.max(descent, m.actualBoundingBoxDescent);
        }
        return { width, ascent, descent, left, right };
    }

    function wrap(ctx, text, size) {
        const lines = [""];
        for (const token of text.match(/[A-Za-z0-9]+|[^A-Za-z0-9]/gu)) {
            if (measure(ctx, token, size).width > 200) return null;
            const last = lines.length - 1;
            if (lines[last] && measure(ctx, lines[last] + token, size).width > 200) lines.push(token);
            else lines[last] += token;
        }
        return lines;
    }

    function plan(ctx, text, index = 0) {
        if (typeof text !== "string" || !text.length || Array.from(text).length > 20)
            throw new RangeError("文案应为 1～20 个字符");
        function candidate(mode, lines, sizes) {
            const measured = lines.map((s, i) => measure(ctx, s, sizes[i]));
            const inkHeight = measured.reduce((n, m) => n + m.ascent + m.descent, 0) + (lines.length - 1) * 12;
            if (measured.some((m) => Math.max(m.width, m.right) - Math.min(0, m.left) > 200) || inkHeight > 252)
                return null;
            return { mode, lines, sizes, measured, inkHeight, index };
        }
        // Keep large type. Width/height decide the layout, not raw character count.
        for (let size = 104; size >= 72; size -= 8) {
            const lines = wrap(ctx, text, size);
            if (lines && lines.length <= 3) {
                const result = candidate("poster", lines, lines.map(() => size));
                if (result) return result;
            }
        }
        for (let split = 1; split < text.length; split++) {
            if (!/[ ,，]/u.test(text[split - 1])) continue;
            for (let size = 88; size >= 64; size -= 8) {
                const result = candidate("split", [text.slice(0, split), text.slice(split)], [size, Math.max(64, size - 8)]);
                if (result) return result;
            }
        }
        const m = measure(ctx, text, 64);
        return { mode: "scroll", lines: [text], sizes: [64], measured: [m], inkHeight: m.ascent + m.descent, index };
    }

    function blurAlpha(pixels, width, height, sigma) {
        // Some mobile Canvas engines lack filter. The same local Gaussian alpha
        // operation keeps the font/preview/upload path offline on those engines.
        const radius = Math.ceil(sigma * 3), kernel = [], temp = new Float32Array(width * height);
        let sum = 0;
        for (let i = -radius; i <= radius; i++) { const value = Math.exp(-i * i / (2 * sigma * sigma)); kernel.push(value); sum += value; }
        for (let i = 0; i < kernel.length; i++) kernel[i] /= sum;
        for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
            let value = 0;
            for (let i = -radius; i <= radius; i++)
                if (x + i >= 0 && x + i < width) value += pixels[(y * width + x + i) * 4 + 3] * kernel[i + radius];
            temp[y * width + x] = value;
        }
        const output = new Uint8ClampedArray(width * height * 4);
        for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
            let value = 0;
            for (let i = -radius; i <= radius; i++)
                if (y + i >= 0 && y + i < height) value += temp[(y + i) * width + x] * kernel[i + radius];
            output[(y * width + x) * 4 + 3] = value;
        }
        return output;
    }

    function render(createCanvas, text, index = 0) {
        const scratch = createCanvas(1, 1).getContext("2d"), layout = plan(scratch, text, index);
        const scrolling = layout.mode === "scroll";
        const width = scrolling ? Math.ceil(Math.max(layout.measured[0].width, layout.measured[0].right) - Math.min(0, layout.measured[0].left)) + 32 : 240;
        const height = scrolling ? 96 : 320;
        if (width > 1536 || layout.inkHeight > height - 20) throw new RangeError("文案渲染尺寸超限");
        const core = createCanvas(width, height), c = core.getContext("2d");
        c.fillStyle = "white";
        let y = Math.round((height - layout.inkHeight) / 2);
        for (let line = 0; line < layout.lines.length; line++) {
            const m = layout.measured[line];
            let x = scrolling ? 16 - Math.min(0, m.left) : Math.round((width - m.width) / 2) + (line % 2 ? 6 : -6);
            y += m.ascent;
            for (const char of layout.lines[line]) {
                c.font = `${layout.sizes[line]}px ${FAMILY}`;
                c.fillText(char, x, y);
                x += measure(scratch, char, layout.sizes[line]).width;
            }
            y += m.descent + 12;
        }
        const halo = createCanvas(width, height), h = halo.getContext("2d");
        // Tall supported glyphs must retain their halo inside the 96px strip.
        const sigma = scrolling && layout.inkHeight > 68 ? 3 : 4;
        const corePixels = c.getImageData(0, 0, width, height).data;
        let haloPixels;
        if ("filter" in h) {
            h.filter = `blur(${sigma}px)`; h.drawImage(core, 0, 0);
            haloPixels = h.getImageData(0, 0, width, height).data;
        } else haloPixels = blurAlpha(corePixels, width, height, sigma);
        const rgba = [corePixels, haloPixels];
        let left = width, top = height, right = -1, bottom = -1;
        for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
            const pos = (y * width + x) * 4 + 3;
            if (rgba[0][pos] || rgba[1][pos]) {
                left = Math.min(left, x); right = Math.max(right, x);
                top = Math.min(top, y); bottom = Math.max(bottom, y);
            }
        }
        if (right < left || left < 1 || top < 1 || right >= width - 1 || bottom >= height - 1)
            throw new RangeError("字体或辉光超出安全边界");
        const w = right - left + 1, ht = bottom - top + 1;
        const layers = rgba.map((pixels) => {
            const alpha = new Uint8Array(w * ht);
            for (let y = 0; y < ht; y++) for (let x = 0; x < w; x++)
                alpha[y * w + x] = pixels[((y + top) * width + left + x) * 4 + 3];
            return alpha;
        });
        return { text, mode: layout.mode, width: w, height: ht, stride: w,
            x: scrolling ? 0 : left, y: scrolling ? [88, 120, 152][index % 3] : top,
            layers, color: COLORS[index % COLORS.length], layout };
    }

    const previewPlanes = new WeakMap();
    // Cache colored planes per rendered item, rather than allocating full RGBA
    // images on every animation frame. Obsolete edits are reclaimed with their keys.
    function preview(ctx, item, x = item.x, coreOpacity = 1, haloOpacity = 0.6) {
        ctx.fillStyle = "black"; ctx.fillRect(0, 0, 240, 320);
        let planes = previewPlanes.get(item);
        if (!planes) {
            const rgb = item.color.slice(1).match(/../g).map((s) => parseInt(s, 16));
            planes = item.layers.map((alpha) => {
                const canvas = ctx.canvas.ownerDocument ? ctx.canvas.ownerDocument.createElement("canvas") : null;
                if (!canvas) throw new Error("Preview requires a browser canvas");
                canvas.width = item.width; canvas.height = item.height;
                const target = canvas.getContext("2d"), data = target.createImageData(item.width, item.height);
                for (let i = 0; i < alpha.length; i++) {
                    data.data[i * 4] = rgb[0]; data.data[i * 4 + 1] = rgb[1];
                    data.data[i * 4 + 2] = rgb[2]; data.data[i * 4 + 3] = alpha[i];
                }
                target.putImageData(data, 0, 0);
                return canvas;
            });
            previewPlanes.set(item, planes);
        }
        for (const layer of [1, 0]) {
            ctx.globalAlpha = layer ? haloOpacity : coreOpacity;
            ctx.drawImage(planes[layer], x, item.y);
        }
        ctx.globalAlpha = 1;
    }

    root.LightboardLayout = { FAMILY, COLORS, DEFAULT_TEXTS, loadFont, measure, plan, render, preview };
    if (typeof module !== "undefined") module.exports = root.LightboardLayout;
})(typeof globalThis !== "undefined" ? globalThis : window);
