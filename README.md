# AI Passport 1024 应援灯牌

适用于 ESP32-C3、240×320 屏幕的工牌与霓虹灯牌固件。通过设备热点和网页配置，字体、预览与保存均可离线使用，无需安装 App。

## 功能与操作

- 工牌：设置照片、姓名、简介，支持方形、圆角和圆形照片。
- 灯牌：黑底霓虹大字，短文案错落分行，长文案横向滚动，内置六条默认文案。
- 最多保存 6 条文案，每条最多 20 个字符；字母、数字、汉字、标点和行内空格各算 1。支持简体中文及常用英文标点，不支持的字符会提示。
- 短按上/下切换工牌与灯牌，长按确认进入设置。
- 所有页面持续亮屏。亮度有 20/40/60/80/100 五档，上下键选择，**按确认保存后生效**。
- 重启恢复已保存的亮度和展示模式。

在设置中开启 Wi-Fi 热点，手机连接 `AI-Passport-XXXX`，用浏览器打开 **http://192.168.4.1/** 。分别编辑并保存工牌或灯牌；完成后按设备任意键关闭热点，再返回展示页。热点无密码，连接后提示无互联网属于正常现象。

## 构建与刷入

安装 PlatformIO Core，在项目根目录执行：

```sh
# 构建
pio run -e ai_passport_badge

# 构建、刷入并查看串口日志
pio run -e ai_passport_badge -t upload -t monitor

# 仅查看日志（Ctrl+C 退出）
pio device monitor -b 115200
```

有多个串口时，刷入加 `--upload-port <串口>`，监控加 `-p <串口>`；可用 `pio device list` 查看。

镜像为 `.pio/build/ai_passport_badge/firmware.bin`。请通过工程上传流程刷入，不要全片擦除，也不要把应用镜像写到 `0x0`。普通上传保留工牌、身份及灯牌数据；旧自定义图片分区保留但不使用。

## 开发检查

```sh
pio test -e native
node --test test/web/*.test.js
python3 -m unittest discover -s tools -p 'test_verify_partition_layout.py'
python3 tools/verify_partition_layout.py partitions.csv .pio/build/ai_passport_badge
```

字体与默认资源已内置，正常构建无需重新制作。网页的真实字体/Canvas 测试需安装 `@napi-rs/canvas@0.1.80`，并用 `LIGHTBOARD_CANVAS_MODULE` 指定模块路径；未配置时对应测试会跳过。字体遵循 [SIL OFL](main/web/badge/lightboard-font-OFL.txt)。
