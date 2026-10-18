(function (root) {
    "use strict";
    const protocol = typeof module !== "undefined" ? require("./lightboard_protocol.js") : root.LightboardProtocol;
    const layout = typeof module !== "undefined" ? require("./lightboard_layout.js") : root.LightboardLayout;
    function checkedPayload(payload) {
        if (!payload || payload.schema !== 1 || payload.font_version !== 1 || payload.layout_version !== 1 ||
            payload.max_items !== 6 || payload.max_chars !== 20 || !Number.isInteger(payload.revision) ||
            payload.revision < 0 || payload.revision > 0xffffffff ||
            !/^[0-9a-f]{16}$/i.test(payload.token || "") || !/[1-9a-f]/i.test(payload.token) ||
            !Array.isArray(payload.texts) || !payload.texts.length || payload.texts.length > 6 ||
            !payload.texts.every(protocol.validText)) throw new Error("设备文案版本不兼容，请刷新页面或更新固件");
        return payload;
    }
    function errorMessage(code, status) {
        const messages = {
            revision_conflict: "设备文案已更新，请重新读取并核对后再保存，当前编辑已保留",
            busy: "设备正在保存或使用旧配置，请稍后重试",
            invalid_token: "配置会话已失效，请重新读取设备后再保存",
            too_large: "文案资源超出设备容量，请减少文案后重试",
            too_many_items: "最多保存 6 条文案",
            text_too_long: "每条文案最多 20 个字符",
            invalid_bundle: "文案或预览数据无效，请修改后重新生成预览",
            invalid_length: "上传未完成，请重新读取设备核对后再保存",
            storage_error: "设备保存失败，请重新读取核对或稍后重试",
            storage_unavailable: "设备存储暂不可用，请检查设备后重试",
            timeout: "上传超时，请重新读取设备核对后再保存",
        };
        return messages[code] || (status === 409 ? messages.revision_conflict : "设备请求失败，请重试");
    }
    async function responsePayload(response) {
        const payload = await response.json();
        if (!response.ok) throw Object.assign(new Error(errorMessage(payload.error, response.status)),
            { known: true, conflict: response.status === 403 ||
                (response.status === 409 && payload.error !== "busy") });
        return payload;
    }
    function createEditor(deps, changed = () => {}) {
        const state = { input: "", savedInput: null, items: [], revision: null, token: "",
            loading: false, preparing: false, uploading: false, fontReady: false,
            conflict: false, uncertain: false, validation: "", message: "", kind: "", preparedInput: null };
        let editVersion = 0, preparation = 0, fontPromise = null;
        const notify = () => changed(state);
        const canSave = () => !state.loading && !state.preparing && !state.uploading &&
            !state.conflict && !state.uncertain && state.fontReady && state.revision !== null &&
            state.items.length > 0 && state.preparedInput === state.input && !state.validation;
        function ensureFont() {
            if (!fontPromise) fontPromise = Promise.resolve().then(deps.loadFont).then(() => {
                state.fontReady = true;
            }).catch(() => {
                fontPromise = null; state.fontReady = false;
                throw new Error("离线字体加载失败，请重新读取后重试；请保持连接设备热点");
            });
            return fontPromise;
        }
        async function prepare() {
            const ticket = ++preparation, input = state.input;
            state.items = []; state.preparedInput = null; state.validation = "";
            state.preparing = true; notify();
            try {
                const texts = protocol.normalize(input);
                await ensureFont();
                const items = [];
                for (const [index, text] of texts.entries()) {
                    if (ticket !== preparation) return false;
                    // Yield to input/paint between entries; an obsolete edit never
                    // publishes its resources after a newer edit or reload.
                    if (deps.yieldFrame) await deps.yieldFrame();
                    if (ticket !== preparation) return false;
                    items.push(await deps.render(text, index));
                }
                if (ticket !== preparation) return false;
                state.items = items; state.preparedInput = input;
            } catch (error) {
                if (ticket !== preparation) return false;
                state.validation = error.message || "预览生成失败，请重新读取后重试";
            } finally {
                if (ticket === preparation) { state.preparing = false; notify(); }
            }
            return state.items.length > 0;
        }
        async function reload() {
            if (state.loading || state.uploading) return false;
            const version = editVersion;
            const dirty = state.savedInput === null ? version > 0 : state.input !== state.savedInput;
            ++preparation; state.preparing = false; state.loading = true; notify();
            try {
                const response = await deps.fetch("/api/lightboard", { cache: "no-store" });
                const payload = checkedPayload(await responsePayload(response));
                const preserve = dirty || version !== editVersion;
                state.savedInput = payload.texts.join("\n");
                if (!preserve) state.input = state.savedInput;
                state.revision = payload.revision; state.token = payload.token;
                state.conflict = state.uncertain = false;
                state.message = preserve ? "已读取最新版本，编辑内容已保留，请核对后保存" : "已读取设备文案";
                state.kind = "";
                await prepare();
                return true;
            } catch (error) {
                state.message = error.message || "无法读取设备文案，请重新读取";
                state.kind = "error";
                return false;
            } finally { state.loading = false; notify(); }
        }
        function edit(input) {
            if (state.uploading) return Promise.resolve(false);
            state.input = input; ++editVersion;
            if (!state.conflict && !state.uncertain) { state.message = ""; state.kind = ""; }
            return prepare();
        }
        async function save() {
            if (!canSave()) return false;
            let bytes;
            try { bytes = protocol.encode(state.items, state.revision); }
            catch (error) { state.validation = error.message; state.kind = "error"; notify(); return false; }
            state.uploading = true; state.message = "正在保存，请勿关闭页面…"; state.kind = ""; notify();
            try {
                const submitted = state.items.map((item) => item.text), revision = state.revision;
                const response = await deps.fetch("/api/lightboard", { method: "POST", headers: {
                    "Content-Type": "application/octet-stream", "X-Passport-Session": state.token }, body: bytes });
                const payload = checkedPayload(await responsePayload(response));
                const next = ((revision + 1) >>> 0) || 1;
                if (payload.ok !== true || payload.revision !== next ||
                    JSON.stringify(payload.texts) !== JSON.stringify(submitted))
                    throw new Error("保存响应无法核对");
                state.revision = payload.revision; state.token = payload.token;
                state.input = state.savedInput = payload.texts.join("\n");
                state.preparedInput = state.input;
                state.message = "已保存，可继续修改；设备会使用新文案"; state.kind = "success";
                return true;
            } catch (error) {
                state.conflict = !!error.conflict;
                state.uncertain = !error.known;
                state.message = error.known ? error.message : "连接中断，保存结果未知，请重新读取核对后再保存，当前编辑已保留";
                state.kind = "error";
                return false;
            } finally { state.uploading = false; notify(); }
        }
        return { state, canSave, edit, reload, save, start: reload };
    }
    const durationUs = (item) => item.mode === "scroll" ? 240000 + (240 + item.width) * 12500 : 3000000;
    function frameAt(items, elapsedMs) {
        if (!items.length) return null;
        let elapsed = Math.floor(Math.max(0, elapsedMs) * 1000) % items.reduce((n, item) => n + durationUs(item), 0);
        let index = 0;
        while (elapsed >= durationUs(items[index])) elapsed -= durationUs(items[index++]);
        const item = items[index], duration = durationUs(item);
        const fade = Math.min(120000, elapsed, duration - elapsed);
        let core = 255, halo = 153, x = item.x;
        if (item.mode === "scroll") x = 240 - Math.floor(Math.min(duration - 240000, Math.max(0, elapsed - 120000)) / 12500);
        else {
            const phase = elapsed % 1500000;
            const t = Math.floor(Math.min(phase, 1500000 - phase) * 1000 / 750000);
            const breath = Math.floor(t * t * (3000 - 2 * t) / 1000000);
            core = 204 + Math.floor(51 * breath / 1000); halo = 89 + Math.floor(64 * breath / 1000);
        }
        return { index, x, core: Math.floor(core * fade / 120000), halo: Math.floor(halo * fade / 120000) };
    }
    const api = { createEditor, checkedPayload, errorMessage, frameAt, durationUs };
    root.LightboardEditor = api;
    if (typeof module !== "undefined") module.exports = api;
    if (typeof document === "undefined") return;
    const get = (id) => document.getElementById(id);
    const form = get("lightboard-form"), input = get("lightboard-texts"), saveButton = get("lightboard-save"),
        reloadButton = get("lightboard-reload"), nextButton = get("lightboard-next"), message = get("lightboard-message"),
        count = get("lightboard-count"), caption = get("lightboard-caption"), canvas = get("lightboard-preview");
    const ctx = canvas.getContext("2d");
    let items = [], origin = 0, lastFrame = -Infinity;
    const editor = createEditor({ fetch: (...args) => fetch(...args), loadFont: layout.loadFont,
        yieldFrame: () => new Promise(requestAnimationFrame), render: (text, index) => layout.render((w, h) => {
            const c = document.createElement("canvas"); c.width = w; c.height = h; return c;
        }, text, index) }, (state) => {
        if (input.value !== state.input) input.value = state.input;
        input.disabled = state.loading || state.uploading;
        saveButton.disabled = !editor.canSave();
        reloadButton.disabled = state.loading || state.uploading;
        saveButton.textContent = state.uploading ? "正在保存…" : "保存灯牌";
        nextButton.disabled = state.items.length < 2;
        const lines = state.input.split("\n").map((line) => line.trim()).filter(Boolean);
        count.textContent = `${lines.length}/6 条` + (lines.length ? ` · 各条字符数：${lines.map((s) => Array.from(s).length).join("、")}/20` : "");
        count.classList.toggle("invalid", !!state.validation);
        message.textContent = state.validation || (state.preparing ? (state.fontReady ? "正在生成预览…" : "正在加载离线字体…") : state.message);
        message.dataset.kind = state.validation ? "error" : state.kind;
        if (items !== state.items) { items = state.items; origin = performance.now(); lastFrame = -Infinity; }
        if (!items.length) {
            ctx.globalAlpha = 1; ctx.fillStyle = "black"; ctx.fillRect(0, 0, 240, 320);
            caption.textContent = state.loading ? "正在读取设备文案…" : "填写有效文案后预览";
        }
    });
    form.addEventListener("submit", (event) => { event.preventDefault(); editor.save(); });
    input.addEventListener("input", () => editor.edit(input.value));
    reloadButton.addEventListener("click", () => editor.reload());
    nextButton.addEventListener("click", () => {
        const now = performance.now(), frame = frameAt(items, now - origin);
        if (!frame) return;
        const next = (frame.index + 1) % items.length;
        origin = now - items.slice(0, next).reduce((n, item) => n + durationUs(item) / 1000, 0);
        lastFrame = -Infinity;
    });
    function animate(now) {
        if (!document.hidden && items.length && now - lastFrame >= 50) {
            const frame = frameAt(items, now - origin), item = items[frame.index];
            layout.preview(ctx, item, frame.x, frame.core / 255, frame.halo / 255);
            caption.textContent = `${frame.index + 1}/${items.length} · ${item.mode === "scroll" ? "霓虹滚动" : "大字应援"}`;
            lastFrame = now;
        }
        requestAnimationFrame(animate);
    }
    requestAnimationFrame(animate);
    editor.start();
})(globalThis);
