(function (root) {
    "use strict";

    const OUTPUT_SIZE = 200;
    const MIN_ZOOM = 1;
    const MAX_ZOOM = 4;
    const ROUNDED_RADIUS = 24;
    const SHAPES = ["square", "rounded", "circle"];
    /* RGB565 has no alpha; transparent preview pixels are composited onto the device canvas. */
    const DEVICE_CANVAS_RGB = Object.freeze([0x0b, 0x0f, 0x0e]);

    function clamp(value, minimum, maximum) {
        return Math.min(maximum, Math.max(minimum, value));
    }

    function normalizedRotation(rotation) {
        return ((Math.round(rotation / 90) * 90) % 360 + 360) % 360;
    }

    function rotatedSize(width, height, rotation) {
        const quarterTurn = normalizedRotation(rotation) % 180 !== 0;
        return { width: quarterTurn ? height : width, height: quarterTurn ? width : height };
    }

    function coverGeometry(width, height, transform, outputWidth = OUTPUT_SIZE,
                           outputHeight = outputWidth) {
        if (!(width > 0 && height > 0 && outputWidth > 0 && outputHeight > 0))
            throw new RangeError("invalid image size");
        const rotation = normalizedRotation(transform.rotation || 0);
        const rotated = rotatedSize(width, height, rotation);
        const zoom = clamp(Number(transform.zoom) || MIN_ZOOM, MIN_ZOOM, MAX_ZOOM);
        /* Rotation changes the source bounds before rectangular cover scaling is calculated. */
        const scale = Math.max(outputWidth / rotated.width, outputHeight / rotated.height) * zoom;
        const renderedWidth = rotated.width * scale;
        const renderedHeight = rotated.height * scale;
        const maxPanX = Math.max(0, (renderedWidth - outputWidth) / 2);
        const maxPanY = Math.max(0, (renderedHeight - outputHeight) / 2);
        return {
            rotation,
            zoom,
            scale,
            panX: maxPanX === 0 ? 0 : clamp(Number(transform.panX) || 0, -maxPanX, maxPanX),
            panY: maxPanY === 0 ? 0 : clamp(Number(transform.panY) || 0, -maxPanY, maxPanY),
            renderedWidth,
            renderedHeight,
            maxPanX,
            maxPanY,
        };
    }

    function rotateTransform(transform, direction) {
        return {
            rotation: normalizedRotation((transform.rotation || 0) + (direction < 0 ? -90 : 90)),
            zoom: clamp(Number(transform.zoom) || MIN_ZOOM, MIN_ZOOM, MAX_ZOOM),
            panX: 0,
            panY: 0,
        };
    }

    function normalizedShape(shape) {
        return SHAPES.includes(shape) ? shape : "square";
    }

    function shapeContains(shape, x, y, outputSize = OUTPUT_SIZE) {
        const normalized = normalizedShape(shape);
        if (x < 0 || y < 0 || x >= outputSize || y >= outputSize) return false;
        if (normalized === "square") return true;
        if (normalized === "circle") {
            const radius = outputSize / 2;
            const dx = x + 0.5 - radius;
            const dy = y + 0.5 - radius;
            return dx * dx + dy * dy <= radius * radius;
        }
        const radius = Math.min(ROUNDED_RADIUS, outputSize / 2);
        if (x + 0.5 >= radius && x + 0.5 <= outputSize - radius) return true;
        if (y + 0.5 >= radius && y + 0.5 <= outputSize - radius) return true;
        const centerX = x + 0.5 < radius ? radius : outputSize - radius;
        const centerY = y + 0.5 < radius ? radius : outputSize - radius;
        const dx = x + 0.5 - centerX;
        const dy = y + 0.5 - centerY;
        return dx * dx + dy * dy <= radius * radius;
    }

    function shapePath(context, shape, outputWidth, outputHeight) {
        const normalized = normalizedShape(shape);
        context.beginPath();
        if (normalized === "square") {
            context.rect(0, 0, outputWidth, outputHeight);
        } else if (normalized === "circle") {
            const radius = Math.min(outputWidth, outputHeight) / 2;
            context.arc(outputWidth / 2, outputHeight / 2, radius, 0, Math.PI * 2);
        } else if (typeof context.roundRect === "function") {
            context.roundRect(0, 0, outputWidth, outputHeight, ROUNDED_RADIUS);
        } else {
            const radius = Math.min(ROUNDED_RADIUS, outputWidth / 2, outputHeight / 2);
            context.moveTo(radius, 0);
            context.lineTo(outputWidth - radius, 0);
            context.quadraticCurveTo(outputWidth, 0, outputWidth, radius);
            context.lineTo(outputWidth, outputHeight - radius);
            context.quadraticCurveTo(outputWidth, outputHeight, outputWidth - radius, outputHeight);
            context.lineTo(radius, outputHeight);
            context.quadraticCurveTo(0, outputHeight, 0, outputHeight - radius);
            context.lineTo(0, radius);
            context.quadraticCurveTo(0, 0, radius, 0);
            context.closePath();
        }
    }

    function renderCrop(context, image, transform, outputWidth = OUTPUT_SIZE,
                        outputHeight = outputWidth, shape = "square") {
        const geometry = coverGeometry(image.width, image.height, transform,
            outputWidth, outputHeight);
        context.clearRect(0, 0, outputWidth, outputHeight);
        context.save();
        /* The clip is part of the final canvas pixels; CSS-only rounding would still upload a square. */
        shapePath(context, shape, outputWidth, outputHeight);
        context.clip();
        context.translate(outputWidth / 2 + geometry.panX,
            outputHeight / 2 + geometry.panY);
        context.rotate(geometry.rotation * Math.PI / 180);
        /* Browsers normalize common EXIF orientation while decoding; rotation here is user-controlled. */
        context.drawImage(image, -image.width * geometry.scale / 2,
            -image.height * geometry.scale / 2, image.width * geometry.scale,
            image.height * geometry.scale);
        context.restore();
        return geometry;
    }

    function canvasPointerDelta(deltaX, deltaY, canvasWidth, canvasHeight,
                                cssWidth, cssHeight) {
        if (!(canvasWidth > 0 && canvasHeight > 0 && cssWidth > 0 && cssHeight > 0))
            throw new RangeError("invalid canvas size");
        /* Pointer events use CSS pixels; crop pan is expressed in backing-canvas pixels. */
        return { x: deltaX * canvasWidth / cssWidth, y: deltaY * canvasHeight / cssHeight };
    }

    function rgbaToRgb565(rgba, pixelCount) {
        if (!rgba || rgba.length < pixelCount * 4) throw new RangeError("insufficient RGBA data");
        const output = new Uint8Array(pixelCount * 2);
        for (let index = 0; index < pixelCount; index += 1) {
            const source = index * 4;
            const alpha = rgba[source + 3] / 255;
            const red = Math.round(rgba[source] * alpha + DEVICE_CANVAS_RGB[0] * (1 - alpha));
            const green = Math.round(rgba[source + 1] * alpha + DEVICE_CANVAS_RGB[1] * (1 - alpha));
            const blue = Math.round(rgba[source + 2] * alpha + DEVICE_CANVAS_RGB[2] * (1 - alpha));
            const value = ((red & 0xf8) << 8) | ((green & 0xfc) << 3) | (blue >> 3);
            /* The firmware envelope stores RGB565 least-significant byte first. */
            output[index * 2] = value & 0xff;
            output[index * 2 + 1] = value >> 8;
        }
        return output;
    }

    function rgb565ToImageData(bytes, width, height, ImageDataType = root.ImageData) {
        if (!bytes || bytes.length !== width * height * 2) throw new RangeError("invalid RGB565 data");
        const rgba = new Uint8ClampedArray(width * height * 4);
        for (let index = 0; index < width * height; index += 1) {
            const value = bytes[index * 2] | (bytes[index * 2 + 1] << 8);
            const target = index * 4;
            const red = (value >> 11) & 0x1f;
            const green = (value >> 5) & 0x3f;
            const blue = value & 0x1f;
            rgba[target] = (red << 3) | (red >> 2);
            rgba[target + 1] = (green << 2) | (green >> 4);
            rgba[target + 2] = (blue << 3) | (blue >> 2);
            rgba[target + 3] = 255;
        }
        return ImageDataType ? new ImageDataType(rgba, width, height) : { data: rgba, width, height };
    }

    async function decodeFile(file) {
        if (!file || !file.type.startsWith("image/")) throw new Error("请选择图片文件");
        if (typeof root.createImageBitmap === "function") {
            try {
                return await root.createImageBitmap(file, { imageOrientation: "from-image" });
            } catch (_) {
                return root.createImageBitmap(file);
            }
        }
        return new Promise((resolve, reject) => {
            const url = URL.createObjectURL(file);
            const image = new Image();
            image.onload = () => { URL.revokeObjectURL(url); resolve(image); };
            image.onerror = () => { URL.revokeObjectURL(url); reject(new Error("图片无法解码")); };
            image.src = url;
        });
    }

    root.BadgeImage = {
        OUTPUT_SIZE, MIN_ZOOM, MAX_ZOOM, clamp, normalizedRotation, rotatedSize,
        ROUNDED_RADIUS, DEVICE_CANVAS_RGB, normalizedShape, shapeContains, coverGeometry, rotateTransform,
        renderCrop, canvasPointerDelta, rgbaToRgb565, rgb565ToImageData, decodeFile,
    };
})(typeof globalThis !== "undefined" ? globalThis : window);
