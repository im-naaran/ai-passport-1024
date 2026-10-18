(function (root) {
    "use strict";

    const encoder = new TextEncoder();
    const MAGIC = 0x46525042;
    const VERSION = 3;
    const HEADER_SIZE = 16;
    const NAME_MAX_BYTES = 48;
    const BIO_MAX_BYTES = 96;
    const IMAGE_BYTES = 80000;
    const SHAPE_VALUES = Object.freeze({ square: 0, rounded: 1, circle: 2 });

    function nameBytes(name) { return encoder.encode(name.trim()); }
    function bioBytes(bio) { return encoder.encode(bio.trim()); }

    function validName(name) {
        const bytes = nameBytes(name);
        return bytes.length > 0 && bytes.length <= NAME_MAX_BYTES &&
            !/[\u0000-\u001f\u007f]/u.test(name);
    }

    function validBio(bio) {
        const bytes = bioBytes(bio || "");
        return bytes.length <= BIO_MAX_BYTES && !/[\u0000-\u001f\u007f]/u.test(bio || "");
    }

    function normalizeProfileShape(shape) {
        if (!Object.prototype.hasOwnProperty.call(SHAPE_VALUES, shape))
            throw new Error("工牌照片形状不兼容");
        return shape;
    }

    function shapeExpansionBlocked(sourceShape, targetShape) {
        const target = normalizeProfileShape(targetShape);
        if (sourceShape === null) return false;
        const source = normalizeProfileShape(sourceShape);
        return SHAPE_VALUES[target] < SHAPE_VALUES[source];
    }

    function buildEnvelope(name, bio, image, shape) {
        const bytes = nameBytes(name);
        const bioData = bioBytes(bio || "");
        const normalizedShape = normalizeProfileShape(shape);
        if (!validName(name)) throw new Error("姓名应为 1–48 个 UTF-8 字节且不能包含控制符");
        if (!validBio(bio)) throw new Error("自定义语句应不超过 96 个 UTF-8 字节且不能包含控制符");
        if (!(image instanceof Uint8Array) || image.length !== IMAGE_BYTES)
            throw new Error("照片数据尺寸不正确");
        const body = new Uint8Array(HEADER_SIZE + bytes.length + bioData.length + image.length);
        const view = new DataView(body.buffer);
        /* This explicit little-endian envelope mirrors the bounded firmware streaming parser. */
        view.setUint32(0, MAGIC, true);
        view.setUint16(4, VERSION, true);
        view.setUint16(6, bytes.length, true);
        view.setUint16(8, bioData.length, true);
        view.setUint8(10, SHAPE_VALUES[normalizedShape]);
        view.setUint8(11, 0);
        view.setUint32(12, image.length, true);
        body.set(bytes, HEADER_SIZE);
        body.set(bioData, HEADER_SIZE + bytes.length);
        body.set(image, HEADER_SIZE + bytes.length + bioData.length);
        return body;
    }

    function saveDisabled(state) {
        return !state.hasImage || !validName(state.name || "") ||
            !validBio(state.bio || "") || state.uploading ||
            !Object.prototype.hasOwnProperty.call(SHAPE_VALUES, state.shape) ||
            shapeExpansionBlocked(state.sourceShape, state.shape);
    }

    function errorMessage(code, status) {
        const messages = {
            invalid_token: "配置会话已失效，请重新进入设备设置页",
            invalid_length: "提交的数据长度不正确",
            invalid_name: "姓名格式不正确",
            invalid_bio: "自定义语句格式不正确",
            invalid_shape: "照片形状无效，请重新选择",
            busy: "设备正在保存，请稍后再试",
            storage_error: "设备保存失败，原工牌资料仍会保留",
            timeout: "上传超时，请重新保存",
        };
        return messages[code] || (status === 409 ? messages.busy : "保存失败，请重试");
    }

    root.BadgeAppLogic = {
        nameBytes, bioBytes, validName, validBio, normalizeProfileShape,
        shapeExpansionBlocked, buildEnvelope, saveDisabled, errorMessage,
    };

    if (typeof document === "undefined") return;

    const elements = {
        profileForm: document.querySelector("#profile-form"),
        profileCanvas: document.querySelector("#preview"),
        profileFile: document.querySelector("#photo"),
        name: document.querySelector("#name"),
        nameCount: document.querySelector("#name-count"),
        bio: document.querySelector("#bio"),
        bioCount: document.querySelector("#bio-count"),
        profileZoom: document.querySelector("#zoom"),
        profileRotate: document.querySelector("#rotate"),
        shapes: Array.from(document.querySelectorAll('input[name="shape"]')),
        profileSave: document.querySelector("#save"),
        profileMessage: document.querySelector("#message"),
        connection: document.querySelector("#connection"),
    };
    const profileContext = elements.profileCanvas.getContext("2d", { willReadFrequently: true });
    const session = { token: "" };
    const profile = {
        name: "", bio: "", shape: "square", sourceShape: null, image: null,
        transform: { rotation: 0, zoom: 1, panX: 0, panY: 0 },
        hasImage: false, uploading: false, pointer: null,
    };

    function showMessage(target, text, kind = "") {
        target.textContent = text;
        target.dataset.kind = kind;
    }

    function closeImage(image) {
        /* ImageBitmap retains decoded pixels; release the previous photo when replacing it. */
        if (image && typeof image.close === "function") image.close();
    }

    function resetTransform(state, zoom) {
        state.transform = { rotation: 0, zoom: 1, panX: 0, panY: 0 };
        zoom.value = "1";
    }

    function syncControls() {
        const locked = profile.uploading;
        profile.name = elements.name.value;
        profile.bio = elements.bio.value;
        elements.nameCount.textContent = `${nameBytes(profile.name).length}/48 字节`;
        elements.nameCount.classList.toggle("invalid", !validName(profile.name));
        elements.bioCount.textContent = `${bioBytes(profile.bio).length}/96 字节`;
        elements.bioCount.classList.toggle("invalid", !validBio(profile.bio));
        elements.profileSave.disabled = saveDisabled(profile);
        elements.profileFile.disabled = locked;
        elements.name.disabled = locked;
        elements.bio.disabled = locked;
        elements.profileZoom.disabled = locked;
        elements.profileRotate.disabled = locked;
        elements.shapes.forEach((input) => { input.disabled = locked; });
    }

    function redrawProfile() {
        if (!profile.image) return;
        profile.transform = Object.assign(profile.transform,
            root.BadgeImage.renderCrop(profileContext, profile.image, profile.transform,
                200, 200, profile.shape));
    }

    function attachPan(canvas) {
        canvas.addEventListener("pointerdown", (event) => {
            if (!profile.image || profile.uploading) return;
            canvas.setPointerCapture(event.pointerId);
            profile.pointer = { clientX: event.clientX, clientY: event.clientY,
                panX: profile.transform.panX, panY: profile.transform.panY };
        });
        canvas.addEventListener("pointermove", (event) => {
            if (!profile.pointer || profile.uploading) return;
            const rect = canvas.getBoundingClientRect();
            const delta = root.BadgeImage.canvasPointerDelta(
                event.clientX - profile.pointer.clientX, event.clientY - profile.pointer.clientY,
                canvas.width, canvas.height, rect.width, rect.height);
            profile.transform.panX = profile.pointer.panX + delta.x;
            profile.transform.panY = profile.pointer.panY + delta.y;
            redrawProfile();
        });
        ["pointerup", "pointercancel"].forEach((type) => canvas.addEventListener(type, () => {
            profile.pointer = null;
        }));
    }

    attachPan(elements.profileCanvas);

    elements.profileFile.addEventListener("change", async () => {
        const file = elements.profileFile.files[0];
        if (!file) return;
        try {
            const decoded = await root.BadgeImage.decodeFile(file);
            closeImage(profile.image);
            profile.image = decoded;
            profile.sourceShape = null;
            resetTransform(profile, elements.profileZoom);
            profile.hasImage = true;
            redrawProfile();
            syncControls();
            showMessage(elements.profileMessage, "照片已载入，可拖动、缩放或旋转");
        } catch (error) {
            showMessage(elements.profileMessage, error.message || "图片无法处理", "error");
        }
    });
    elements.profileZoom.addEventListener("input", () => {
        profile.transform.zoom = Number(elements.profileZoom.value);
        redrawProfile();
    });
    elements.profileRotate.addEventListener("click", () => {
        if (!profile.image) return;
        profile.transform = root.BadgeImage.rotateTransform(profile.transform, 1);
        redrawProfile();
    });
    elements.name.addEventListener("input", () => { syncControls(); });
    elements.bio.addEventListener("input", () => { syncControls(); });
    elements.shapes.forEach((input) => input.addEventListener("change", () => {
        if (!input.checked) return;
        profile.shape = normalizeProfileShape(input.value);
        redrawProfile();
        syncControls();
        if (shapeExpansionBlocked(profile.sourceShape, profile.shape)) {
            showMessage(elements.profileMessage,
                "当前设备照片的角落已在上次保存时丢失，请重新选择原图后再保存", "error");
        } else {
            showMessage(elements.profileMessage, "照片形状已更新，可继续调整或保存");
        }
    }));

    async function responseError(response) {
        let payload = {};
        try { payload = await response.json(); } catch (_) { /* use stable HTTP fallback */ }
        return errorMessage(payload.error, response.status);
    }

    async function loadProfile() {
        const profileResponse = await fetch("/api/profile", { cache: "no-store" });
        if (!profileResponse.ok) throw new Error(await responseError(profileResponse));
        const payload = await profileResponse.json();
        const storedShape = normalizeProfileShape(payload.shape);
        elements.name.value = payload.name || "";
        elements.bio.value = payload.bio || "";
        profile.shape = storedShape;
        elements.shapes.forEach((input) => { input.checked = input.value === storedShape; });
        const photoResponse = await fetch("/api/photo", { cache: "no-store" });
        if (!photoResponse.ok) throw new Error(await responseError(photoResponse));
        const bytes = new Uint8Array(await photoResponse.arrayBuffer());
        const source = document.createElement("canvas");
        source.width = source.height = 200;
        source.getContext("2d").putImageData(
            root.BadgeImage.rgb565ToImageData(bytes, 200, 200), 0, 0);
        closeImage(profile.image);
        profile.image = source;
        profile.sourceShape = storedShape;
        profile.hasImage = true;
        resetTransform(profile, elements.profileZoom);
        /* Stored RGB565 has no alpha, so reapply its saved mask before showing the reflected image. */
        redrawProfile();
        syncControls();
    }

    elements.profileForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        if (saveDisabled(profile)) return;
        profile.uploading = true;
        syncControls();
        showMessage(elements.profileMessage, "正在保存，请勿关闭页面…");
        const rgba = profileContext.getImageData(0, 0, 200, 200).data;
        const body = buildEnvelope(profile.name, profile.bio,
            root.BadgeImage.rgbaToRgb565(rgba, 200 * 200), profile.shape);
        try {
            const response = await fetch("/api/profile", { method: "POST",
                headers: { "Content-Type": "application/octet-stream",
                    "X-Passport-Session": session.token }, body });
            if (!response.ok) throw Object.assign(
                new Error(await responseError(response)), { known: true });
                showMessage(elements.profileMessage, "已保存，可继续修改或按设备任意键退出", "success");
        } catch (error) {
            showMessage(elements.profileMessage,
                error.known ? error.message : "连接已中断，保存结果未知，请查看设备工牌", "error");
        } finally {
            profile.uploading = false;
            syncControls();
        }
    });

    async function loadInitial() {
        try {
            const statusResponse = await fetch("/api/status", { cache: "no-store" });
            if (!statusResponse.ok) throw new Error("无法读取设备状态");
            const status = await statusResponse.json();
            /* The token exists only for this physical hotspot session and is never persisted locally. */
            session.token = String(status.token || "");
            elements.connection.textContent = status.connected ? "手机已连接" : "已连接设备热点";
            await loadProfile();
        } catch (error) {
            elements.connection.textContent = "设备连接异常";
            showMessage(elements.profileMessage, error.message || "页面初始化失败", "error");
        } finally {
            syncControls();
        }
    }

    syncControls();
    loadInitial();
})(typeof globalThis !== "undefined" ? globalThis : window);
