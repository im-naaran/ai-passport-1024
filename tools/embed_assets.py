Import("env")

# .incbin inputs are invisible to PlatformIO's C dependency scanner. Without these explicit
# dependencies an incremental firmware build can silently keep an older configuration page.
embedded_asset_object = env.File(
    "$BUILD_DIR/main/services/badge/badge_assets.c.o"
)
embedded_asset_sources = [
    env.File("$PROJECT_DIR/main/web/badge/index.html"),
    env.File("$PROJECT_DIR/main/web/badge/style.css"),
    env.File("$PROJECT_DIR/main/web/badge/badge_image.js"),
    env.File("$PROJECT_DIR/main/web/badge/app.js"),
    env.File("$PROJECT_DIR/main/web/badge/lightboard.woff2"),
    env.File("$PROJECT_DIR/main/web/badge/lightboard-font-OFL.txt"),
    env.File("$PROJECT_DIR/main/web/badge/lightboard_charset.js"),
    env.File("$PROJECT_DIR/main/web/badge/lightboard_layout.js"),
    env.File("$PROJECT_DIR/main/web/badge/lightboard_protocol.js"),
    env.File("$PROJECT_DIR/main/web/badge/lightboard_app.js"),
    env.File("$PROJECT_DIR/main/resources/lightboard_defaults.bin"),
]
env.Depends(embedded_asset_object, embedded_asset_sources)
